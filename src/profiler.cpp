#include "profiler.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <alsa/asoundlib.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <vector>

#ifdef HAVE_JACK
#include <jack/jack.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kStepMs = 10000; // measuring time per buffer size
constexpr int kSettleMs = 700; // let a stream get going before measuring

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }

    return QString::fromUtf8(f.readAll());
}

// Runs a helper program to completion; false if it is missing or failed.
bool runTool(const QString &program, const QStringList &args, QByteArray *output = nullptr)
{
    QProcess p;
    p.start(program, args);

    if (!p.waitForFinished(8000)) {
        p.kill();
        p.waitForFinished(500);

        return false;
    }

    if (output) {
        *output = p.readAllStandardOutput();
    }

    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// Output delay of the running playback stream as ALSA reports it, in ms.
double alsaDelayMs(int card)
{
    const QString base = QString("/proc/asound/card%1/pcm0p/sub0/").arg(card);
    static const QRegularExpression delayRe(R"(^delay\s*:\s*(\d+))", QRegularExpression::MultilineOption);
    static const QRegularExpression rateRe(R"(^rate:\s*(\d+))", QRegularExpression::MultilineOption);
    const auto delay = delayRe.match(readFile(base + "status"));
    const auto rate = rateRe.match(readFile(base + "hw_params"));

    if (!delay.hasMatch() || !rate.hasMatch() || rate.captured(1).toInt() <= 0) {
        return -1;
    }

    return 1000.0 * delay.captured(1).toDouble() / rate.captured(1).toInt();
}

// ------------------------------------------------------------ ALSA

ProfileStep alsaStep(int card, int rate, int frames, const std::atomic<bool> &stop)
{
    ProfileStep step;
    step.frames = frames;
    snd_pcm_t *pcm = nullptr;
    const QByteArray name = "hw:" + QByteArray::number(card) + ",0";
    int rc = snd_pcm_open(&pcm, name.constData(), SND_PCM_STREAM_PLAYBACK, 0);

    if (rc < 0) {
        step.error = rc == -EBUSY ? QStringLiteral("the interface is in use by another program")
                                  : QString::fromLocal8Bit(snd_strerror(rc));
        return step;
    }

    // The interface's native format: six channels of 32-bit samples.
    const unsigned channels = 6;
    snd_pcm_uframes_t period = frames;
    snd_pcm_uframes_t buffer = 2UL * frames;
    snd_pcm_hw_params_t *hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    rc = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    rc = rc < 0 ? rc : snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE);
    rc = rc < 0 ? rc : snd_pcm_hw_params_set_channels(pcm, hw, channels);
    rc = rc < 0 ? rc : snd_pcm_hw_params_set_rate(pcm, hw, rate, 0);
    rc = rc < 0 ? rc : snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
    rc = rc < 0 ? rc : snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
    rc = rc < 0 ? rc : snd_pcm_hw_params(pcm, hw);

    if (rc < 0) {
        step.error = QString::fromLocal8Bit(snd_strerror(rc));
        snd_pcm_close(pcm);

        return step;
    }

    step.actualFrames = static_cast<int>(period);
    step.actualRate = rate;

    const std::vector<qint32> silence(period * channels, 0);
    const auto end = Clock::now() + std::chrono::milliseconds(kStepMs);
    double delaySum = 0;
    int delayCount = 0;

    while (!stop && Clock::now() < end) {
        const snd_pcm_sframes_t written = snd_pcm_writei(pcm, silence.data(), period);

        if (written < 0) {
            if (written == -EPIPE) {
                ++step.underruns;
            }

            if (snd_pcm_recover(pcm, static_cast<int>(written), 1) < 0) {
                step.error = QString::fromLocal8Bit(snd_strerror(static_cast<int>(written)));
                break;
            }

            continue;
        }

        snd_pcm_sframes_t delay = 0;

        if (snd_pcm_delay(pcm, &delay) == 0 && delay >= 0) {
            delaySum += static_cast<double>(delay);
            ++delayCount;
        }
    }

    if (delayCount > 0) {
        step.latencyMs = 1000.0 * delaySum / delayCount / rate;
    }

    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);

    return step;
}

// ------------------------------------------------------------ PipeWire

struct PwState {
    int device = -1; // PipeWire id of the interface
    int activeProfile = -1;
    int proProfile = -1; // index of "pro-audio"
    QString proSink;
    QStringList nodes; // node names of the interface
};

PwState pwQuery(int card)
{
    PwState state;
    QByteArray json;

    if (!runTool("pw-dump", {}, &json)) {
        return state;
    }

    for (const auto &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        const QString type = o["type"].toString();
        const QJsonObject info = o["info"].toObject();
        const QJsonObject props = info["props"].toObject();

        if (type.endsWith(":Device") && !props["api.alsa.card"].isUndefined()
            && props["api.alsa.card"].toVariant().toInt() == card) {
            const QJsonObject params = info["params"].toObject();
            state.device = o["id"].toInt();
            state.activeProfile = params["Profile"].toArray().first().toObject()["index"].toInt(-1);

            for (const auto &profile : params["EnumProfile"].toArray()) {
                if (profile.toObject()["name"].toString() == "pro-audio") {
                    state.proProfile = profile.toObject()["index"].toInt(-1);
                }
            }
        } else if (type.endsWith(":Node") && !props["alsa.card"].isUndefined()
                   && props["alsa.card"].toVariant().toInt() == card) {
            const QString name = props["node.name"].toString();
            state.nodes << name;

            if (props["media.class"].toString() == "Audio/Sink" && name.contains(".pro-output")) {
                state.proSink = name;
            }
        }
    }

    return state;
}

struct PwTop {
    bool ok = false;
    int errors = 0; // xruns of the interface's nodes, cumulative
    int quantum = 0;
    int rate = 0;
};

PwTop pwTop(const QStringList &nodes)
{
    PwTop top;
    QByteArray text;

    if (!runTool("pw-top", {"-b", "-n", "2"}, &text)) {
        return top;
    }

    // Columns: S ID QUANT RATE WAIT BUSY W/Q B/Q ERR [FORMAT...] NAME.
    // Later iterations overwrite earlier ones.
    QHash<QString, int> errors;

    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> col = line.simplified().split(' ');
        const QString name = QString::fromUtf8(col.last());

        if (col.size() < 10 || col[0].size() != 1 || !nodes.contains(name)) {
            continue;
        }

        errors[name] = col[8].toInt();

        if (col[2].toInt() > 0) { // only the driver node carries the graph's values
            top.quantum = col[2].toInt();
            top.rate = col[3].toInt();
        }
    }

    for (const int e : std::as_const(errors)) {
        top.errors += e;
    }

    top.ok = !errors.isEmpty();

    return top;
}

void pwSetProfile(const PwState &state, int profile)
{
    runTool("wpctl", {"set-profile", QString::number(state.device), QString::number(profile)});
}

// The graph's buffer size and rate are shared by everything PipeWire runs, so
// a test has to force them for its duration. 0 means "not forced"; -1 that
// PipeWire is not there.
struct PwForced {
    int quantum = -1;
    int rate = -1;
};

PwForced pwForced()
{
    PwForced forced;
    QByteArray text;

    if (QStandardPaths::findExecutable("pw-metadata").isEmpty()
        || !runTool("pw-metadata", {"-n", "settings", "0"}, &text)) {
        return forced;
    }

    static const QRegularExpression quantumRe(R"(key:'clock\.force-quantum' value:'(\d+)')");
    static const QRegularExpression rateRe(R"(key:'clock\.force-rate' value:'(\d+)')");
    const auto quantum = quantumRe.match(QString::fromUtf8(text));
    const auto rate = rateRe.match(QString::fromUtf8(text));

    if (quantum.hasMatch() && rate.hasMatch()) {
        forced.quantum = quantum.captured(1).toInt();
        forced.rate = rate.captured(1).toInt();
    }

    return forced;
}

void pwForce(const PwForced &forced)
{
    runTool("pw-metadata", {"-n", "settings", "0", "clock.force-quantum", QString::number(forced.quantum)});
    runTool("pw-metadata", {"-n", "settings", "0", "clock.force-rate", QString::number(forced.rate)});
}

// ------------------------------------------------------------ JACK

#ifdef HAVE_JACK

struct JackTest {
    jack_port_t *out[2] = {};
    std::atomic<int> xruns{0};
};

// NOLINTNEXTLINE(misc-const-correctness): the signature is given by JACK
int jackProcess(jack_nframes_t frames, void *arg)
{
    const auto *test = static_cast<const JackTest *>(arg);

    for (jack_port_t *port : test->out) {
        auto *buffer = static_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(port, frames));
        std::fill_n(buffer, frames, 0.0F);
    }

    return 0;
}

int jackXrun(void *arg)
{
    ++static_cast<JackTest *>(arg)->xruns;

    return 0;
}

// Physical playback ports of the interface.
QStringList jackPlaybackPorts(jack_client_t *client)
{
    QStringList ports;
    const char **names =
        jack_get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): null-terminated C array
    for (const char *const *n = names; n && *n; ++n) {
        const QString name = QString::fromUtf8(*n);

        if (name.contains("Traktor", Qt::CaseInsensitive)) {
            ports << name;
        }
    }

    jack_free(static_cast<void *>(names));

    return ports;
}

#endif // HAVE_JACK

} // namespace

// ------------------------------------------------------------ ProfileResult

QString apiName(AudioApi api)
{
    switch (api) {
    case AudioApi::Alsa:
        return QStringLiteral("ALSA");
    case AudioApi::PipeWire:
        return QStringLiteral("PipeWire");
    case AudioApi::Jack:
        return QStringLiteral("JACK");
    }

    return {};
}

QString apiUnavailable(AudioApi api)
{
    if (api == AudioApi::PipeWire) {
        for (const char *tool : {"pw-dump", "pw-top", "pw-cat", "pw-metadata", "wpctl"}) {
            if (QStandardPaths::findExecutable(tool).isEmpty()) {
                return QString("PipeWire's tools are not installed (%1 is missing). Install PipeWire "
                               "and WirePlumber.")
                    .arg(tool);
            }
        }

        if (!runTool("pw-dump", {})) {
            return QStringLiteral("PipeWire is installed but not running. Start it, for example with "
                                  "\"systemctl --user enable --now pipewire wireplumber\".");
        }
    }

    if (api == AudioApi::Jack) {
#ifdef HAVE_JACK
        jack_client_t *client = jack_client_open("traktor-audio6-probe", JackNoStartServer, nullptr);

        if (!client) {
            return QStringLiteral("No JACK server is running. Install and start JACK (jack2), or "
                                  "pipewire-jack if you use PipeWire.");
        }

        jack_client_close(client);
#else
        return QStringLiteral("This build has no JACK support. Install the JACK development files and "
                              "build the panel again.");
#endif
    }

    return {}; // ALSA is what the interface's kernel driver provides
}

const ProfileStep *ProfileResult::suggested() const
{
    const ProfileStep *best = nullptr;

    if (aborted) {
        return best; // an incomplete run says nothing about the smaller sizes
    }

    // Steps run from the largest size down; stop at the first one with trouble.
    for (const ProfileStep &step : steps) {
        if (!step.error.isEmpty() || step.underruns > 0) {
            break;
        }

        best = &step;
    }

    return best;
}

QString ProfileResult::summary() const
{
    QString text = QString("Buffer test: %1, %2 Hz\n\n").arg(apiName(api)).arg(rate);

    if (!error.isEmpty()) {
        return text + "The test could not be run: " + error + ".\n";
    }

    text += "  Buffer        Latency    Underruns  Result\n";

    for (const ProfileStep &step : steps) {
        QString verdict = step.underruns == 0 ? "OK" : "dropouts";
        QString latency = step.latencyMs >= 0 ? QString("%1 ms").arg(step.latencyMs, 0, 'f', 1) : "-";

        if (!step.error.isEmpty()) {
            verdict = "failed: " + step.error;
        } else if (step.actualFrames != step.frames || step.actualRate != rate) {
            verdict += QString(" (ran with %1 samples at %2 Hz)").arg(step.actualFrames).arg(step.actualRate);
        }

        text += QString("  %1  %2  %3  %4\n")
                    .arg(QString("%1 samples").arg(step.frames), -12)
                    .arg(latency, -9)
                    .arg(step.underruns, -9)
                    .arg(verdict);
    }

    text += '\n';

    if (aborted) {
        text += "The test was stopped early, so there is no suggestion.\n";
    } else if (const ProfileStep *best = suggested()) {
        text += QString("Suggested settings: %1, %2 Hz, %3 samples")
                    .arg(apiName(api))
                    .arg(best->actualRate > 0 ? best->actualRate : rate)
                    .arg(best->actualFrames > 0 ? best->actualFrames : best->frames);

        if (best->latencyMs >= 0) {
            text += QString(" (about %1 ms output latency)").arg(best->latencyMs, 0, 'f', 1);
        }

        text += ".\nChoose the next larger buffer if you hear crackling under load.\n";
    } else if (!steps.isEmpty()) {
        text += "No buffer size ran without dropouts. Try another audio API or a lower sample rate.\n";
    }

    return text;
}

const ProfileResult *bestOf(const QList<ProfileResult> &results)
{
    // An unknown latency never beats a measured one.
    auto latency = [](const ProfileResult &result) {
        const double ms = result.suggested()->latencyMs;

        return ms >= 0 ? ms : std::numeric_limits<double>::max();
    };

    const ProfileResult *best = nullptr;

    for (const ProfileResult &result : results) {
        if (result.suggested() && (!best || latency(result) < latency(*best))) {
            best = &result;
        }
    }

    return best;
}

QString comparison(const QList<ProfileResult> &results)
{
    QString text = "Comparison of the audio APIs\n\n  API       Smallest clean buffer  Latency\n";

    for (const ProfileResult &result : results) {
        const ProfileStep *step = result.suggested();
        QString buffer = "none";
        QString latency = "-";

        if (!result.error.isEmpty()) {
            buffer = "not tested";
        } else if (step) {
            buffer = QString("%1 samples").arg(step->actualFrames > 0 ? step->actualFrames : step->frames);
            latency = step->latencyMs >= 0 ? QString("%1 ms").arg(step->latencyMs, 0, 'f', 1) : "-";
        }

        text += QString("  %1  %2  %3\n").arg(apiName(result.api), -8).arg(buffer, -21).arg(latency);
    }

    if (const ProfileResult *best = bestOf(results)) {
        const ProfileStep *step = best->suggested();
        text += QString("\nLowest latency without dropouts: %1 with %2 samples at %3 Hz.\n")
                    .arg(apiName(best->api))
                    .arg(step->actualFrames > 0 ? step->actualFrames : step->frames)
                    .arg(step->actualRate > 0 ? step->actualRate : best->rate);
    } else {
        text += "\nNo API ran without dropouts.\n";
    }

    return text;
}

// ------------------------------------------------------------ Profiler

Profiler::Profiler(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<ProfileResult>>();
}

Profiler::~Profiler()
{
    stop();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Profiler::start(const QList<AudioApi> &apis, int card, int rate, bool demo)
{
    if (m_running.exchange(true)) {
        return;
    }

    if (m_thread.joinable()) {
        m_thread.join(); // the previous run has finished
    }

    m_stop = false;
    m_thread = std::thread([this, apis, card, rate, demo] { run(apis, card, rate, demo); });
}

void Profiler::stop()
{
    m_stop = true;
}

bool Profiler::wait(int ms) const
{
    const auto end = Clock::now() + std::chrono::milliseconds(ms);

    while (!m_stop && Clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return !m_stop;
}

void Profiler::run(const QList<AudioApi> &apis, int card, int rate, bool demo)
{
    QList<ProfileResult> results;

    for (const AudioApi api : apis) {
        if (m_stop) {
            break;
        }

        results.append(runOne(api, card, rate, demo));
    }

    m_running = false;
    emit finished(results);
}

ProfileResult Profiler::runOne(AudioApi api, int card, int rate, bool demo)
{
    ProfileResult result;
    result.api = api;
    result.rate = rate;

    if (demo) {
        // Made-up numbers that differ per API so that a comparison has a winner.
        const int index = static_cast<int>(api);
        const int cleanFrom[] = {64, 128, 256}; // smallest size without dropouts

        for (const int frames : kProfileSizes) {
            emit progress(QString("Testing %1 with %2 samples ...").arg(apiName(api)).arg(frames));

            if (!wait(300)) {
                break;
            }

            ProfileStep step;
            step.frames = step.actualFrames = frames;
            step.actualRate = rate;
            step.latencyMs = (2000.0 - 400.0 * index) * frames / rate;
            step.underruns = frames < cleanFrom[index] ? 7 : 0;
            result.steps.append(step);
        }
    } else if (api == AudioApi::Alsa) {
        runAlsa(card, result);
    } else if (api == AudioApi::PipeWire) {
        runPipeWire(card, result);
    } else {
        runJack(result);
    }

    result.aborted = m_stop;

    return result;
}

void Profiler::runAlsa(int card, ProfileResult &result)
{
    for (const int frames : kProfileSizes) {
        if (m_stop) {
            break;
        }

        emit progress(QString("Testing ALSA with %1 samples ...").arg(frames));
        ProfileStep step = alsaStep(card, result.rate, frames, m_stop);

        // PipeWire needs a moment to let go of the device, e.g. after the
        // previous test of a comparison.
        for (int i = 0; i < 10 && result.steps.isEmpty() && !step.error.isEmpty() && wait(300); ++i) {
            step = alsaStep(card, result.rate, frames, m_stop);
        }

        result.steps.append(step);

        // Without the device there is no point in trying the other sizes.
        if (result.steps.size() == 1 && !result.steps.first().error.isEmpty()) {
            result.error = result.steps.first().error;
            result.steps.clear();
            break;
        }
    }
}

void Profiler::runPipeWire(int card, ProfileResult &result)
{
    PwState state = pwQuery(card);

    if (state.device < 0 || state.proProfile < 0) {
        result.error = "PipeWire is not running or does not offer a Pro Audio profile for the interface";
        return;
    }

    // Pro Audio hands the device to PipeWire without the desktop's stereo
    // mapping and its extra buffering, which is what low-latency setups use.
    const int previous = state.activeProfile;
    const PwForced forcedBefore = pwForced();

    if (previous != state.proProfile) {
        emit progress("Switching the interface to the Pro Audio profile ...");
        pwSetProfile(state, state.proProfile);

        for (int i = 0; i < 10 && state.proSink.isEmpty() && wait(300); ++i) {
            state = pwQuery(card);
        }
    }

    if (state.proSink.isEmpty()) {
        result.error = "the Pro Audio output of the interface did not appear";
    }

    for (const int frames : kProfileSizes) {
        if (m_stop || !result.error.isEmpty()) {
            break;
        }

        emit progress(QString("Testing PipeWire with %1 samples ...").arg(frames));
        ProfileStep step;
        step.frames = frames;
        pwForce({.quantum = frames, .rate = result.rate});
        QProcess player;
        player.start("pw-cat", {
                                   "--playback",
                                   "--raw",
                                   "--target",
                                   state.proSink,
                                   "--format",
                                   "s16",
                                   "--channels",
                                   "2",
                                   "--rate",
                                   QString::number(result.rate),
                                   "--latency",
                                   QString::number(frames),
                                   "/dev/zero",
                               });

        if (!player.waitForStarted(3000)) {
            result.error = "pw-cat could not be started";
            break;
        }

        wait(kSettleMs);
        const PwTop before = pwTop(state.nodes);
        double delaySum = 0;
        int delayCount = 0;

        for (int i = 0; i < kStepMs / 100 && wait(100); ++i) {
            const double delay = alsaDelayMs(card);

            if (delay >= 0) {
                delaySum += delay;
                ++delayCount;
            }
        }

        const PwTop after = pwTop(state.nodes);
        player.kill();
        player.waitForFinished(1000);

        if (!before.ok || !after.ok) {
            step.error = "no data from pw-top";
        } else {
            step.underruns = std::max(0, after.errors - before.errors);
            step.actualFrames = after.quantum;
            step.actualRate = after.rate;
            step.latencyMs = delayCount > 0 ? delaySum / delayCount : -1;
        }

        result.steps.append(step);
    }

    // Put PipeWire back the way it was.
    pwForce(forcedBefore);

    if (previous != state.proProfile) {
        pwSetProfile(state, previous);
    }
}

#ifdef HAVE_JACK

void Profiler::runJack(ProfileResult &result)
{
    jack_client_t *client = jack_client_open("traktor-audio6-test", JackNoStartServer, nullptr);

    if (!client) {
        result.error = "no JACK server is running";
        return;
    }

    JackTest test;
    test.out[0] = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    test.out[1] = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    jack_set_process_callback(client, jackProcess, &test);
    jack_set_xrun_callback(client, jackXrun, &test);
    QStringList playback = jackPlaybackPorts(client);

    // The ports can take a moment to show up, e.g. right after a profile change.
    for (int i = 0; i < 10 && playback.isEmpty() && wait(300); ++i) {
        playback = jackPlaybackPorts(client);
    }

    if (!test.out[0] || !test.out[1] || playback.isEmpty() || jack_activate(client) != 0) {
        result.error = playback.isEmpty() ? "the interface is not available as a JACK device"
                                          : "the JACK client could not be started";
        jack_client_close(client);

        return;
    }

    for (int i = 0; i < 2 && i < playback.size(); ++i) {
        jack_connect(client, jack_port_name(test.out[i]), playback[i].toUtf8().constData());
    }

    const jack_nframes_t original = jack_get_buffer_size(client);
    // With PipeWire as the JACK server, a buffer size change through JACK would
    // stay forced for good and the rate could not be chosen at all, so the
    // graph is set up through PipeWire instead.
    const PwForced forcedBefore = pwForced();
    const bool pipewire = forcedBefore.quantum >= 0;
    jack_port_t *sink = jack_port_by_name(client, playback.first().toUtf8().constData());

    for (const int frames : kProfileSizes) {
        if (m_stop) {
            break;
        }

        emit progress(QString("Testing JACK with %1 samples ...").arg(frames));
        ProfileStep step;
        step.frames = frames;

        if (pipewire) {
            pwForce({.quantum = frames, .rate = result.rate});
        } else if (jack_set_buffer_size(client, frames) != 0) {
            step.error = "the server refused this buffer size";
            result.steps.append(step);
            continue;
        }

        wait(kSettleMs);
        test.xruns = 0;
        wait(kStepMs);
        step.underruns = test.xruns;
        step.actualFrames = static_cast<int>(jack_get_buffer_size(client));
        step.actualRate = static_cast<int>(jack_get_sample_rate(client));

        jack_latency_range_t range{};
        jack_port_get_latency_range(sink, JackPlaybackLatency, &range);
        const jack_nframes_t latency = range.max > 0 ? range.max : step.actualFrames;
        step.latencyMs = 1000.0 * latency / step.actualRate;
        result.steps.append(step);
    }

    // Put the server back the way it was.
    if (pipewire) {
        jack_client_close(client);
        pwForce(forcedBefore);
    } else {
        jack_set_buffer_size(client, original);
        jack_client_close(client);
    }
}

#else

void Profiler::runJack(ProfileResult &result)
{
    result.error = "this build has no JACK support";
}

#endif
