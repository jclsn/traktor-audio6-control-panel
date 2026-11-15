// System Performance Test: plays silence through the interface with one audio
// API at a series of buffer sizes and reports which ones run without dropouts.
#pragma once
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <thread>

enum class AudioApi : std::uint8_t { Alsa, PipeWire, Jack };

QString apiName(AudioApi api);

// Why an API cannot be tested on this system, with a hint what to install or
// start; empty if it can.
QString apiUnavailable(AudioApi api);

// Buffer sizes that are tried, in frames, largest first.
inline constexpr int kProfileSizes[] = {1024, 512, 256, 128, 64};

struct ProfileStep {
    int frames = 0; // requested buffer size
    int actualFrames = 0; // what the API really ran with
    int actualRate = 0;
    double latencyMs = -1; // measured output latency, -1 if unknown
    int underruns = 0;
    QString error; // set if the step could not be run
};

struct ProfileResult {
    AudioApi api = AudioApi::Alsa;
    int rate = 0; // requested sample rate
    QList<ProfileStep> steps;
    QString error; // set if the test could not run at all
    bool aborted = false;

    // The smallest buffer size that ran clean together with all larger ones;
    // null if not even the largest size was free of dropouts.
    const ProfileStep *suggested() const;

    // Plain-text report with the per-size results and the suggested settings.
    QString summary() const;
};

// The run with the lowest output latency at its suggested buffer size; null if
// no API produced a suggestion.
const ProfileResult *bestOf(const QList<ProfileResult> &results);

// Plain-text table that puts the suggestions of several APIs side by side.
QString comparison(const QList<ProfileResult> &results);

Q_DECLARE_METATYPE(QList<ProfileResult>)

class Profiler : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Profiler)

public:
    explicit Profiler(QObject *parent = nullptr);
    ~Profiler() override;

    // Tests the given APIs one after the other. card: ALSA card index of the
    // interface. demo: fabricate results quickly.
    void start(const QList<AudioApi> &apis, int card, int rate, bool demo);
    void stop(); // abort; finished() is still emitted

    bool running() const { return m_running; }

signals:
    void progress(const QString &text);
    void finished(const QList<ProfileResult> &results);

private:
    void run(const QList<AudioApi> &apis, int card, int rate, bool demo);
    ProfileResult runOne(AudioApi api, int card, int rate, bool demo);
    void runAlsa(int card, ProfileResult &result);
    void runPipeWire(int card, ProfileResult &result);
    void runJack(ProfileResult &result);
    bool wait(int ms) const; // false if the test was stopped meanwhile

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
};
