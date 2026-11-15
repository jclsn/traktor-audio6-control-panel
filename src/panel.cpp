#include "panel.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <initializer_list>
#include <utility>

using namespace ui;

namespace {

// Content height per page. The first one is taller than the original because
// of the Audio API selection and the longer explanation.
const int kPageHeights[] = {314, 397, 473};
const int kPageX = kFrameX + 1, kPageY = kFrameY + 1;
const int kRates[] = {44100, 48000, 88200, 96000};
const int kBufferSizes[] = {64, 128, 256, 512, 1024, 2048};
const char *const kManualUrl =
    "https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/traktor/"
    "traktor_audio_6_manual_english.pdf";

QString number(bool available, int n)
{
    return available ? QString::number(n) : QStringLiteral("-");
}

// Traffic-light colour for a scheduling latency, matching the legend.
QColor latencyColor(long us)
{
    if (us < 500) {
        return theme().good;
    }

    return us < 1000 ? theme().warn : theme().bad;
}

const char *const kDarkModeKey = "dark_mode";
const char *const kApiKey = "audio_api";
const char *const kRateKey = "sample_rate";
const char *const kBufferKey = "process_buffer";

QSettings panelSettings()
{
    // NOLINTNEXTLINE(modernize-return-braced-init-list): the constructor is explicit
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "traktor-audio6", "panel");
}

} // namespace

// ------------------------------------------------------------ LatencyTest

void LatencyTest::start()
{
    if (m_run.exchange(true)) {
        return;
    }

    m_current = -1;
    m_thread = std::thread([this] {
        timespec next{};
        clock_gettime(CLOCK_MONOTONIC, &next);
        long windowMax = 0;
        int n = 0;

        while (m_run) {
            next.tv_nsec += 1'000'000;
            if (next.tv_nsec >= 1'000'000'000) {
                next.tv_nsec -= 1'000'000'000;
                ++next.tv_sec;
            }

            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            const long late = (now.tv_sec - next.tv_sec) * 1'000'000L + (now.tv_nsec - next.tv_nsec) / 1000;
            windowMax = std::max(windowMax, late);

            if (late > m_max) {
                m_max = late;
            }

            if (++n == 250) { // publish the worst value of each 250 ms window
                m_current = windowMax;
                windowMax = 0;
                n = 0;
            }

            if (late > 100'000) { // we fell far behind (e.g. suspend): resynchronise
                clock_gettime(CLOCK_MONOTONIC, &next);
            }
        }
    });
}

void LatencyTest::stop()
{
    m_run = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ------------------------------------------------------------ Panel

QVariant Panel::savedDarkMode()
{
    return panelSettings().value(kDarkModeKey);
}

Panel::Panel(bool demo, bool followSystem)
    : m_dev(demo)
    , m_canvas(new Canvas)
{
    setWindowTitle("Native Instruments Traktor Audio 6 Control Panel");

    setCentralWidget(m_canvas);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    const char *names[] = {"Audio Settings", "Channel Setup", "Diagnostics"};
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);
    QMenu *view = menuBar()->addMenu("&View");
    auto *tabGroup = new QButtonGroup(this);

    for (int i = 0; i < 3; ++i) {
        view->addAction(names[i], this, [this, i] { selectPage(i); });
        auto *tab = new SideTab(names[i], m_canvas);
        tab->move(kTabX, kFrameY + kTabPitch * i);

        connect(tab, &QAbstractButton::clicked, this, [this, i] { selectPage(i); });
        tabGroup->addButton(tab);
        m_tabs << tab;
        m_pages << new QWidget(m_canvas);
    }

    view->addSeparator();
    QAction *dark = view->addAction("Dark Mode");
    dark->setCheckable(true);
    dark->setChecked(darkMode());

    auto *darkSwitch = new MoonButton(m_canvas);
    darkSwitch->move(kWidth - 8 - 44 - darkSwitch->width(), 18); // in the banner, left of the badge
    darkSwitch->setChecked(darkMode());

    auto applyDark = [dark, darkSwitch](bool on) {
        setDarkMode(on);
        dark->setChecked(on);
        darkSwitch->setChecked(on);
    };

    auto chooseDark = [applyDark](bool on) {
        applyDark(on);
        panelSettings().setValue(kDarkModeKey, on);
    };

    connect(dark, &QAction::triggered, this, chooseDark);
    connect(darkSwitch, &QAbstractButton::clicked, this, chooseDark);

    if (followSystem) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [applyDark](Qt::ColorScheme scheme) {
                    if (!savedDarkMode().isValid()) {
                        applyDark(scheme == Qt::ColorScheme::Dark);
                    }
                });
    }

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("Troubleshooting Guide", this, [] { QDesktopServices::openUrl(QUrl(kManualUrl)); });
    help->addAction("About", this, [this] {
        QMessageBox::about(this, "About",
                           "<b>Traktor Audio 6 Control Panel for Linux</b><br>Version " APP_VERSION
                           "<br><br>Unofficial re-creation of the Native Instruments control panel.<br>"
                           "Port settings: ALSA and USB vendor requests &middot; "
                           "Performance test: ALSA, PipeWire, JACK");
    });

    buildAudioPage();
    buildChannelPage();
    buildDiagnosticsPage();
    selectPage(0);

    connect(&m_profiler, &Profiler::progress, this, [this](const QString &text) {
        m_testProgress = text;
        poll();
    });

    connect(&m_profiler, &Profiler::finished, this, &Panel::testFinished);

    if (demo) {
        m_pwOk = m_journalOk = true;
    } else {
        connect(&m_dump, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
            if (st == QProcess::NormalExit && code == 0) {
                parseDump(m_dump.readAllStandardOutput());
            } else {
                m_pwOk = false;
            }
        });

        connect(&m_dump, &QProcess::errorOccurred, this, [this] { m_pwOk = false; });
        connect(&m_top, &QProcess::finished, this, [this] { parseTop(m_top.readAllStandardOutput()); });
        connect(&m_journal, &QProcess::started, this, [this] { m_journalOk = true; });
        connect(&m_journal, &QProcess::errorOccurred, this, [this] { m_journalOk = false; });
        connect(&m_journal, &QProcess::finished, this, [this] { m_journalOk = false; });
        connect(&m_journal, &QProcess::readyReadStandardOutput, this, &Panel::readJournal);
        m_journal.start("journalctl", {"-k", "-f", "-n", "0", "-o", "cat"});

        auto *slow = new QTimer(this);

        connect(slow, &QTimer::timeout, this, [this] {
            startDump();
            startTop();
        });

        slow->start(2000);
        startDump();
    }

    auto *fast = new QTimer(this);

    connect(fast, &QTimer::timeout, this, &Panel::poll);
    fast->start(500);
    poll();
    setChannelMode(false);
}

Panel::~Panel()
{
    m_profiler.stop(); // its destructor waits for the run to wind down

    for (QProcess *p : {&m_dump, &m_top, &m_journal}) {
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(500);
        }
    }
}

void Panel::selectPage(int index)
{
    m_page = index;
    const int h = kPageHeights[index];

    for (int i = 0; i < 3; ++i) {
        m_pages[i]->setVisible(i == index);
    }

    m_tabs[index]->setChecked(true);
    m_canvas->setActiveTab(QRect(m_tabs[index]->pos(), QSize(kTabWidth, kTabHeight)));
    m_pages[index]->setGeometry(kPageX, kPageY, kFrameRight - kFrameX - 2,
                                h - kFrameY - kFrameBottomMargin - 2);
    m_canvas->setFixedSize(kWidth, h);

    if (index == 2) {
        startTop();
    }
}

// Positions below are relative to the page, which starts at (114, 56) of the canvas.

void Panel::buildAudioPage()
{
    QWidget *page = m_pages[0];

    // Settings to use in the audio application. Nothing here is applied to the
    // system: they select what the System Performance Test tries, and the test
    // puts its suggestion back into them.
    auto *proc = new Group(page);
    proc->setGeometry(18, 18, 314, 129);
    proc->addStrip(10, "Performance Test Settings");
    m_api = new Combo;

    for (const AudioApi api : {AudioApi::Alsa, AudioApi::PipeWire, AudioApi::Jack}) {
        m_api->addItem(apiName(api), static_cast<int>(api));
    }

    m_rate = new Combo;

    for (const int r : kRates) {
        m_rate->addItem(QString("%1 Hz").arg(r), r);
    }

    m_buffer = new Combo;

    for (const int b : kBufferSizes) {
        m_buffer->addItem(QString("%1 samples").arg(b), b);
    }

    proc->addRow(43, m_api, "Audio API");
    proc->addRow(70, m_rate, "Sample Rate");
    proc->addRow(97, m_buffer, "Process Buffer");

    const QSettings saved = panelSettings();
    m_api->setCurrentIndex(qMax(0, m_api->findData(saved.value(kApiKey, 0))));
    m_rate->setCurrentIndex(qMax(0, m_rate->findData(saved.value(kRateKey, 44100))));
    m_buffer->setCurrentIndex(qMax(0, m_buffer->findData(saved.value(kBufferKey, 256))));

    for (const Combo *combo : {m_api, m_rate, m_buffer}) {
        connect(combo, &QComboBox::activated, this, [this] {
            saveAudioChoice();
            updateAudio();
        });
    }

    auto *mon = new Group(page);
    mon->setGeometry(350, 18, 314, 129);
    mon->addStrip(10, "Device Monitor");
    mon->addRow(43, m_state = new ValueBox, "Processing State");
    mon->addRow(70, m_latency = new ValueBox, "Output Latency");
    mon->addRow(97, m_streamRate = new ValueBox, "Stream Sample Rate");

    auto *notice = new Notice(page);
    notice->setGeometry(18, 165, notice->width(), 66); // room for three lines
    m_notices << notice;
}

void Panel::buildChannelPage()
{
    QWidget *page = m_pages[1];

    m_tabCurrent = new SubTab("Current", page);
    m_tabCurrent->move(18, 19);
    m_tabCurrent->setChecked(true);
    m_tabStartup = new SubTab("Startup", page);
    m_tabStartup->move(125, 19);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_tabCurrent);
    modeGroup->addButton(m_tabStartup);

    connect(m_tabCurrent, &QAbstractButton::clicked, this, [this] { setChannelMode(false); });
    connect(m_tabStartup, &QAbstractButton::clicked, this, [this] { setChannelMode(true); });

    for (int ch = 0; ch < 2; ++ch) {
        auto *g = new Group(page);
        g->setGeometry(18 + 166 * ch, 60, 148, 185);
        g->setHeading(ch == 0 ? "A" : "B");
        g->addStrip(29, "Input Routing", Qt::AlignLeft);
        g->addStrip(93, "Input Sensitivity", Qt::AlignLeft);
        m_direct[ch] = new Check("Direct Thru", g);
        m_direct[ch]->move(19, 62);
        m_phono[ch] = new Radio("Phono", g);
        m_phono[ch]->move(19, 126);
        m_line[ch] = new Radio("Line", g);
        m_line[ch]->move(19, 153);

        for (const QAbstractButton *b :
             std::initializer_list<const QAbstractButton *>{m_direct[ch], m_phono[ch], m_line[ch]}) {
            connect(b, &QAbstractButton::clicked, this, &Panel::portEdited);
        }
    }

    auto *hatch = new Hatch(page);
    hatch->setGeometry(350, 60, 314, 185);

    auto *notice = new Notice(page);
    notice->move(18, 263);
    m_notices << notice;

    connect(notice->lock, &QAbstractButton::clicked, this, [this] {
        m_unlocked = !m_unlocked;
        if (!m_unlocked) {
            m_startupLoaded = false; // locked again without saving: discard edits
        }

        updateChannel();
    });

    connect(notice->save, &QAbstractButton::clicked, this, [this] {
        if (!m_dev.writeStartup(shownPorts())) {
            QMessageBox::warning(this, windowTitle(),
                                 "The startup settings could not be saved to the interface.");
            return;
        }

        m_unlocked = false;
        m_startupLoaded = false; // show what the interface really stored
        updateChannel();
    });
}

void Panel::buildDiagnosticsPage()
{
    QWidget *page = m_pages[2];
    auto group = [page](int x, int y, int h, const char *title) {
        auto *g = new Group(page);
        g->setGeometry(x, y, 314, h);
        g->addStrip(10, title);

        return g;
    };

    auto button = [](Group *g, const char *text, int y) {
        auto *b = new Button(text, g);
        b->move(19, y);

        return b;
    };

    Group *drop = group(18, 18, 131, "Drop-Out Detection");
    drop->addRow(43, m_usbIoBox = new ValueBox, "USB I/O Errors");
    drop->addRow(70, m_underrunBox = new ValueBox, "Buffer Underruns");

    connect(button(drop, "Reset", 96), &QAbstractButton::clicked, this, [this] {
        m_usbIo = 0;
        m_xrunBase = m_xruns;
        updateDiagnostics();
    });

    Group *conn = group(350, 18, 131, "Connection Monitor");
    conn->addRow(43, m_clientsBox = new ValueBox, "Number of Clients");
    conn->addRow(70, m_usbDataBox = new ValueBox, "USB Data Errors");

    connect(button(conn, "Reset", 96), &QAbstractButton::clicked, this, [this] {
        m_usbData = 0;
        updateDiagnostics();
    });

    Group *perf = group(18, 165, 155, "System Performance Test");
    perf->addRow(43, m_latCurrent = new ValueBox, "Current Latency");
    perf->addRow(70, m_latMax = new ValueBox, "Maximum Latency");

    connect(button(perf, "Reset", 96), &QAbstractButton::clicked, this, [this] {
        m_latTest.reset();
        updateDiagnostics();
    });

    // As in the original, Start and Reset only drive the latency monitor.
    m_latStart = button(perf, "Start", 123);

    connect(m_latStart, &QAbstractButton::clicked, this, [this] {
        m_latTest.running() ? m_latTest.stop() : m_latTest.start();
        updateDiagnostics();
    });

    auto *legend = new Legend(perf);
    legend->setGeometry(127, 96, 70, 45);

    // Additions to the original: test the buffer sizes of the selected audio
    // API, or of every API in turn with a comparison.
    m_testBuffers = new Button("Test Buffers", perf);
    m_testBuffers->move(204, 96);

    connect(m_testBuffers, &QAbstractButton::clicked, this,
            [this] { startTest({static_cast<AudioApi>(m_api->currentData().toInt())}); });

    m_testApis = new Button("Test APIs", perf);
    m_testApis->move(204, 123);

    connect(m_testApis, &QAbstractButton::clicked, this,
            [this] { startTest({AudioApi::Alsa, AudioApi::PipeWire, AudioApi::Jack}); });

    Group *info = group(350, 165, 155, "Information");
    info->addRow(43, m_driverBox = new ValueBox, "Driver Version");
    info->addRow(70, m_firmwareBox = new ValueBox, "Firmware Version");
    auto *show = new Button("Show");
    info->addRow(96, show, "Troubleshooting Guide");

    connect(show, &QAbstractButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(kManualUrl)); });
    m_driverBox->setText(driverVersion());

    // Addition to the original: the realtime priority limit of the session.
    // The button takes the place of the value while there is nothing to show.
    info->addRow(123, m_realtimeBox = new ValueBox, "Realtime Priority");
    m_realtimeSetup = new Button("Set Up", info);
    m_realtimeSetup->move(m_realtimeBox->pos());

    connect(m_realtimeSetup, &QAbstractButton::clicked, this, &Panel::offerRealtimeSetup);

    auto *notice = new Notice(page);
    notice->move(18, 339);
    m_notices << notice;
}

// ------------------------------------------------------------ periodic update

void Panel::poll()
{
    m_dev.refresh();
    const bool connected = m_dev.connected();

    if (!connected && m_profiler.running()) {
        m_profiler.stop();
    }

    if (!connected) {
        m_notices[0]->set(Notice::Kind::Error, "Interface not connected.");
        m_notices[2]->set(Notice::Kind::Error, "Interface not connected.");
    } else {
        m_notices[0]->set(Notice::Kind::Info,
                          "Nothing on this page is applied to the interface or the system. On Linux every "
                          "audio application chooses its own audio API, sample rate and buffer size. These "
                          "fields only select what Test Buffers and Test APIs on the Diagnostics page "
                          "measure, and show their suggestion to enter in your application.");
        m_notices[2]->set(Notice::Kind::Ok,
                          m_profiler.running() ? m_testProgress : QString("Interface connected."));
    }

    updateAudio();
    updateChannel();
    updateDiagnostics();
}

void Panel::updateAudio()
{
    const bool connected = m_dev.connected();
    const bool editable = connected && !m_profiler.running();

    for (Combo *combo : {m_api, m_rate, m_buffer}) {
        combo->setEnabled(editable);
    }

    m_state->setEnabled(connected);
    m_latency->setEnabled(connected);
    m_streamRate->setEnabled(connected);
    QString state = "-";

    if (connected) {
        state = m_dev.streaming() ? "Streaming" : "Idle";
    }

    m_state->setText(state);

    // Measured on the running stream, otherwise what the chosen buffer implies.
    double ms = 1000.0 * m_buffer->currentData().toInt() / m_rate->currentData().toInt();

    if (m_dev.streaming() && m_dev.delayFrames() >= 0 && m_dev.hwRate() > 0) {
        ms = 1000.0 * static_cast<double>(m_dev.delayFrames()) / m_dev.hwRate();
    }

    m_latency->setText(connected ? QString("%1 ms").arg(ms, 0, 'f', 1) : "-");
    m_streamRate->setText(connected && m_dev.hwRate() > 0 ? QString("%1 Hz").arg(m_dev.hwRate()) : "-");
}

void Panel::updateChannel()
{
    const bool connected = m_dev.connected();
    Notice *notice = m_notices[1];
    bool editable = false;

    if (m_startupMode) {
        if (!connected) {
            m_unlocked = false;
            m_startupLoaded = false;
        } else if (!m_startupLoaded) {
            PortSettings stored;
            m_startupLoaded = m_dev.readStartup(stored);

            if (m_startupLoaded) {
                showPorts(stored);
            }
        }

        editable = m_unlocked && m_startupLoaded;

        if (!connected) {
            notice->set(Notice::Kind::Error, "Interface not connected.");
        } else if (!m_startupLoaded) {
            notice->set(Notice::Kind::Error,
                        "The startup settings could not be read. Check the access rights of the USB device.");
        } else {
            notice->set(Notice::Kind::Info,
                        m_unlocked ? "Choose the settings the interface should power on with, then click "
                                     "Save to store them in the interface."
                                   : "Startup settings are stored in the interface. It powers on with them, "
                                     "also without a computer. Click the lock to make changes.");
        }

        notice->showLock(m_startupLoaded, !m_unlocked);
        notice->save->setVisible(editable);
    } else {
        editable = connected;
        notice->showLock(false);
        notice->save->setVisible(false);

        if (connected) {
            notice->set(
                Notice::Kind::Info,
                "Current settings take effect immediately on this computer. They are set through ALSA, "
                "which saves them at shutdown and restores them when the interface is plugged in.");
        } else {
            notice->set(Notice::Kind::Error, "Interface not connected.");
        }

        // Follow changes made elsewhere (alsamixer ...).
        bool busy = false;

        for (int ch = 0; ch < 2; ++ch) {
            busy |= m_direct[ch]->isDown() || m_phono[ch]->isDown() || m_line[ch]->isDown();
        }

        if (connected && !busy) {
            showPorts(m_dev.ports());
        }
    }

    for (int ch = 0; ch < 2; ++ch) {
        m_direct[ch]->setEnabled(editable);
        m_phono[ch]->setEnabled(editable);
        m_line[ch]->setEnabled(editable);
    }
}

void Panel::updateDiagnostics()
{
    const bool connected = m_dev.connected();
    for (ValueBox *b : {m_usbIoBox, m_underrunBox, m_clientsBox, m_usbDataBox, m_firmwareBox}) {
        b->setEnabled(connected);
    }

    m_usbIoBox->setText(number(connected && m_journalOk, m_usbIo));
    m_usbDataBox->setText(number(connected && m_journalOk, m_usbData));
    m_underrunBox->setText(number(connected && m_pwOk, qMax(0, m_xruns - m_xrunBase)));
    m_clientsBox->setText(number(connected && m_pwOk, m_clients));
    m_firmwareBox->setText(connected ? m_dev.firmware() : "-");

    // New limits only reach the session at the next login.
    const int realtime = rtprioLimit();
    const bool pending = realtime == 0 && rtprioSetup().action == RtprioSetup::Action::LogInAgain;
    m_realtimeSetup->setVisible(realtime == 0 && !pending);
    m_realtimeBox->setVisible(!m_realtimeSetup->isVisible());
    m_realtimeBox->setText(pending ? QString("Log in again") : QString::number(realtime));

    auto show = [](ValueBox *box, long us) {
        box->setText(QString::fromUtf8("%1 µs").arg(us), latencyColor(us));
    };

    if (m_latTest.hasData()) {
        show(m_latCurrent, m_latTest.currentUs());
        show(m_latMax, m_latTest.maxUs());
    } else {
        m_latCurrent->setText("-");
        m_latMax->setText("-");
    }

    // A buffer test runs the latency monitor itself.
    m_latStart->setText(m_latTest.running() ? "Stop" : "Start");
    m_latStart->setEnabled(!m_profiler.running());
    m_testBuffers->setText(m_profiler.running() ? "Stop" : "Test Buffers");
    m_testBuffers->setEnabled(connected);
    m_testApis->setEnabled(connected && !m_profiler.running());
}

// ------------------------------------------------------------ Channel Setup

void Panel::setChannelMode(bool startup)
{
    m_startupMode = startup;
    m_unlocked = false;
    (startup ? m_tabStartup : m_tabCurrent)->setChecked(true);
    m_startupLoaded = false;

    if (!startup) {
        showPorts(m_dev.ports());
    }

    updateChannel();
}

void Panel::showPorts(const PortSettings &s)
{
    const bool direct[] = {s.directA, s.directB};
    const bool phono[] = {s.phonoA, s.phonoB};

    for (int ch = 0; ch < 2; ++ch) {
        m_direct[ch]->setChecked(direct[ch]);
        (phono[ch] ? m_phono[ch] : m_line[ch])->setChecked(true);
    }
}

PortSettings Panel::shownPorts() const
{
    return {
        .directA = m_direct[0]->isChecked(),
        .directB = m_direct[1]->isChecked(),
        .phonoA = m_phono[0]->isChecked(),
        .phonoB = m_phono[1]->isChecked(),
    };
}

void Panel::portEdited()
{
    if (m_startupMode) {
        return; // edits stay in the widgets until Save
    }

    m_dev.setPorts(shownPorts());
    showPorts(m_dev.ports()); // show what the hardware really accepted
}

// ------------------------------------------------------------ PipeWire / kernel log

void Panel::startDump()
{
    if (!m_dev.demo() && m_dump.state() == QProcess::NotRunning) {
        m_dump.start("pw-dump", QStringList());
    }
}

void Panel::parseDump(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        return;
    }

    QSet<int> ours;
    QSet<QString> names;
    QList<QPair<int, int>> links;

    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString type = o["type"].toString();
        const QJsonObject info = o["info"].toObject();
        const QJsonObject props = info["props"].toObject();

        if (type.endsWith(":Node")) {
            QJsonValue card = props["alsa.card"];
            if (card.isUndefined()) {
                card = props["api.alsa.pcm.card"];
            }

            if (m_dev.connected() && !card.isUndefined() && card.toVariant().toInt() == m_dev.card()) {
                ours << o["id"].toInt();
                names << props["node.name"].toString();
            }
        } else if (type.endsWith(":Link")) {
            links.append({info["output-node-id"].toInt(), info["input-node-id"].toInt()});
        }
    }

    QSet<int> peers;

    for (const auto &[out, in] : links) {
        if (ours.contains(out) != ours.contains(in)) {
            peers << (ours.contains(out) ? in : out);
        }
    }

    m_clients = static_cast<int>(peers.size());
    m_nodeNames = std::move(names);
    m_pwOk = true;
}

// ------------------------------------------------------------ System Performance Test

void Panel::saveAudioChoice() const
{
    if (m_dev.demo()) {
        return;
    }

    QSettings settings = panelSettings();
    settings.setValue(kApiKey, m_api->currentData());
    settings.setValue(kRateKey, m_rate->currentData());
    settings.setValue(kBufferKey, m_buffer->currentData());
}

void Panel::startTest(const QList<AudioApi> &apis)
{
    if (m_profiler.running()) {
        m_profiler.stop();
        return;
    }

    // Leave out what this system does not have, and say what is missing. A
    // setup with only ALSA and PulseAudio, for instance, has neither PipeWire
    // nor JACK.
    QList<AudioApi> usable;
    QStringList missing;

    for (const AudioApi api : apis) {
        const QString reason = m_dev.demo() ? QString() : apiUnavailable(api);

        if (reason.isEmpty()) {
            usable << api;
        } else {
            missing << QString("%1: %2").arg(apiName(api), reason);
        }
    }

    if (!missing.isEmpty()) {
        const QString rest = usable.isEmpty() ? QString("There is nothing left to test.")
                                              : QString("The test continues without them.");
        QMessageBox::information(this, "Audio API not available",
                                 QString("These audio APIs cannot be tested on this system:\n\n%1\n\n%2")
                                     .arg(missing.join("\n\n"), rest));
    }

    if (usable.isEmpty()) {
        return;
    }

    m_testProgress = "Starting the test ...";
    m_latTest.reset();
    m_latTest.start();
    m_profiler.start(usable, m_dev.card(), m_rate->currentData().toInt(), m_dev.demo());
    poll();
}

void Panel::checkRealtimePriority()
{
    // Nothing to offer if the user is already covered and only a login is missing.
    if (!m_dev.demo() && rtprioLimit() == 0 && rtprioSetup().action != RtprioSetup::Action::LogInAgain) {
        offerRealtimeSetup();
    }
}

void Panel::offerRealtimeSetup()
{
    const RtprioSetup setup = rtprioSetup();
    QMessageBox ask(QMessageBox::Question, "Realtime priority",
                    QString("Your session may not use realtime scheduling (the rtprio limit is 0). Audio "
                            "applications then cannot give their audio thread priority over other "
                            "programs, which causes dropouts at small buffer sizes.\n\n"
                            "%1\n\n"
                            "%2 It needs administrator rights. Afterwards you need to log out of your "
                            "desktop session and log in again, or restart the computer.")
                        .arg(setup.question, setup.detail),
                    QMessageBox::NoButton, this);
    const QAbstractButton *accept = ask.addButton("Set Up", QMessageBox::AcceptRole);
    ask.addButton("Not Now", QMessageBox::RejectRole);
    ask.exec();

    if (ask.clickedButton() != accept) {
        return;
    }

    // pkexec shows the desktop's password prompt.
    auto *install = new QProcess(this);

    connect(install, &QProcess::finished, this, [this, install, setup](int code) {
        install->deleteLater();

        if (code == 0) {
            QMessageBox::information(
                this, "Realtime priority",
                QString("%1\n\nLog out of your desktop session and log in again, or restart the "
                        "computer. The new limits only apply to sessions started after this; audio "
                        "applications then use them on their own.")
                    .arg(setup.done));
        } else {
            QMessageBox::warning(this, "Realtime priority", "The limits could not be set up.");
        }

        updateDiagnostics();
    });

    connect(install, &QProcess::errorOccurred, this, [this, install] {
        install->deleteLater();
        QMessageBox::warning(this, "Realtime priority", "pkexec is needed to set up the limits.");
    });

    install->start("pkexec", setup.command);
}

void Panel::testFinished(const QList<ProfileResult> &results)
{
    m_latTest.stop();

    QString text;

    for (const ProfileResult &result : results) {
        text += result.summary() + '\n';
    }

    if (results.size() > 1) {
        text += comparison(results) + '\n';
    }

    text += QString::fromUtf8("Worst scheduling latency during the test: %1 µs.\n").arg(m_latTest.maxUs());
    const int realtime = rtprioLimit();
    text += realtime > 0
              ? QString("Realtime priority limit of this session: %1.\n").arg(realtime)
              : QString("Realtime scheduling is not available to this session (rtprio limit 0).\n");

    // Take over the suggestion, of the best API if several were compared.
    if (const ProfileResult *best = bestOf(results)) {
        const ProfileStep *step = best->suggested();
        const int buffer = m_buffer->findData(step->actualFrames > 0 ? step->actualFrames : step->frames);
        const int api = m_api->findData(static_cast<int>(best->api));

        if (buffer >= 0 && api >= 0) {
            m_buffer->setCurrentIndex(buffer);
            m_api->setCurrentIndex(api);
            saveAudioChoice();
            text += "\nThe Audio Settings page has been set to the suggested values.\n";
        }
    }

    std::fputs(qPrintable(text), stdout);
    std::fflush(stdout);
    poll();

    QMessageBox box(QMessageBox::NoIcon, "Test Results",
                    "<pre style='font-family: monospace'>" + text.toHtmlEscaped() + "</pre>", QMessageBox::Ok,
                    this);
    box.exec();
}

void Panel::startTop()
{
    // Xrun counters are only needed while the Diagnostics page is showing.
    if (!m_dev.demo() && m_pwOk && m_page == 2 && m_top.state() == QProcess::NotRunning) {
        m_top.start("pw-top", {"-b", "-n", "2"});
    }
}

void Panel::parseTop(const QByteArray &text)
{
    // Columns: S ID QUANT RATE WAIT BUSY W/Q B/Q ERR [FORMAT...] NAME
    QHash<QString, int> errors;

    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> col = line.simplified().split(' ');
        if (col.size() >= 10 && col[0].size() == 1 && m_nodeNames.contains(QString::fromUtf8(col.last()))) {
            errors[QString::fromUtf8(col.last())] = col[8].toInt();
        }
    }

    if (errors.isEmpty()) {
        return;
    }

    m_xruns = 0;

    for (int e : errors) {
        m_xruns += e;
    }

    if (m_xruns < m_xrunBase) { // counters restarted (device re-plugged)
        m_xrunBase = 0;
    }
}

void Panel::readJournal()
{
    static const QRegularExpression dataErr(
        R"(reset \S+ USB device|babble|transaction error|protocol error|EPROTO|EILSEQ|error -(71|84)\b|\bCRC\b)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ioErr(R"(urb|time(d )?out|x-?run|cannot|unable|error|fail)",
                                          QRegularExpression::CaseInsensitiveOption);
    while (m_journal.canReadLine()) {
        const QString line = QString::fromUtf8(m_journal.readLine());
        if (m_dev.usbPort().isEmpty() || !line.contains("usb " + m_dev.usbPort() + ":")) {
            continue;
        }

        if (dataErr.match(line).hasMatch()) {
            ++m_usbData;
        } else if (ioErr.match(line).hasMatch()) {
            ++m_usbIo;
        }
    }
}

// ------------------------------------------------------------ development aid

void Panel::saveScreenshots(const QString &dir)
{
    QDir().mkpath(dir);
    auto shot = [&](const QString &name) {
        poll();
        qApp->processEvents();
        grab().save(QDir(dir).filePath(name + ".png"));
    };

    selectPage(0);
    shot("audio");
    selectPage(1);
    shot("channel-current");
    setChannelMode(true);
    shot("channel-startup");
    m_notices[1]->lock->click();
    shot("channel-startup-unlocked");
    m_notices[1]->lock->click();
    setChannelMode(false);
    selectPage(2);
    shot("diagnostics");
}
