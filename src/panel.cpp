#include "panel.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <ctime>
#include <initializer_list>
#include <utility>

using namespace ui;

namespace {

const int kPageHeights[] = {271, 397, 473}; // content height per page, from the original
const int kPageX = kFrameX + 1, kPageY = kFrameY + 1;
const int kRates[] = {44100, 48000, 88200, 96000};
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
        return kGood;
    }

    return us < 1000 ? kWarn : kBad;
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

Panel::Panel(bool demo)
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
        tab->move(8, 54 + 33 * i);

        connect(tab, &QAbstractButton::clicked, this, [this, i] { selectPage(i); });
        tabGroup->addButton(tab);
        m_tabs << tab;
        m_pages << new QWidget(m_canvas);
    }

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("Troubleshooting Guide", this, [] { QDesktopServices::openUrl(QUrl(kManualUrl)); });
    help->addAction("About", this, [this] {
        QMessageBox::about(this, "About",
                           "<b>Traktor Audio 6 Control Panel for Linux</b><br>Version " APP_VERSION
                           "<br><br>Unofficial re-creation of the Native Instruments control panel.<br>"
                           "Port settings: ALSA (snd-usb-audio) &middot; Audio settings: PipeWire");
    });

    buildAudioPage();
    buildChannelPage();
    buildDiagnosticsPage();
    selectPage(0);

    if (demo) {
        m_pwOk = m_journalOk = true;
        m_pwRate = 44100;
        m_pwQuantum = 256;
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

    auto *proc = new Group(page);
    proc->setGeometry(18, 18, 314, 102);
    proc->addStrip(10, "Audio Processing");
    m_rate = new Combo;

    for (int r : kRates) {
        m_rate->addItem(QString("%1 Hz").arg(r), r);
    }

    m_buffer = new Combo;
    proc->addRow(43, m_rate, "Sample Rate");
    proc->addRow(70, m_buffer, "Process Buffer");

    connect(m_rate, &QComboBox::activated, this, [this](int i) {
        m_pwRate = m_rate->itemData(i).toInt();
        setPipewireSetting("clock.force-rate", m_pwRate);
    });

    connect(m_buffer, &QComboBox::activated, this, [this](int i) {
        m_pwQuantum = m_buffer->itemData(i).toInt();
        setPipewireSetting("clock.force-quantum", m_pwQuantum);
    });

    auto *mon = new Group(page);
    mon->setGeometry(350, 18, 314, 102);
    mon->addStrip(10, "Device Monitor");
    mon->addRow(43, m_state = new ValueBox, "Processing State");
    mon->addRow(70, m_latency = new ValueBox, "Output Latency");

    auto *notice = new Notice(page);
    notice->move(18, 138);
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
        g->addStrip(93, "Input Sens", Qt::AlignLeft);
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
            showPorts(loadStartup()); // locked again without saving: discard edits
        }

        updateChannel();
    });

    connect(notice->save, &QAbstractButton::clicked, this, [this] {
        if (!saveStartup(shownPorts())) {
            QMessageBox::warning(this, windowTitle(), "The startup settings could not be saved.");
            return;
        }

        m_unlocked = false;
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

    m_latStart = button(perf, "Start", 123);

    connect(m_latStart, &QAbstractButton::clicked, this, [this] {
        m_latTest.running() ? m_latTest.stop() : m_latTest.start();
        updateDiagnostics();
    });

    auto *legend = new Legend(perf);
    legend->setGeometry(127, 96, 177, 45);

    Group *info = group(350, 165, 155, "Information");
    info->addRow(43, m_driverBox = new ValueBox, "Driver Version");
    info->addRow(70, m_firmwareBox = new ValueBox, "Firmware Version");
    auto *show = new Button("Show");
    info->addRow(96, show, "Troubleshooting Guide");

    connect(show, &QAbstractButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(kManualUrl)); });
    m_driverBox->setText(driverVersion());

    auto *notice = new Notice(page);
    notice->move(18, 339);
    m_notices << notice;
}

// ------------------------------------------------------------ periodic update

void Panel::poll()
{
    m_dev.refresh();
    const bool connected = m_dev.connected();

    // Emulate the device's startup memory while the panel is open (see README).
    if (!m_firstPoll && connected && !m_wasConnected && !m_dev.demo()) {
        QTimer::singleShot(1500, this, [this] {
            m_dev.refresh();
            if (m_dev.connected()) {
                m_dev.setPorts(loadStartup());
            }
        });
    }

    m_firstPoll = false;
    m_wasConnected = connected;

    for (Notice *n : {m_notices[0], m_notices[2]}) {
        n->set(connected ? Notice::Kind::Ok : Notice::Kind::Error,
               connected ? "Interface connected." : "Interface not connected.");
    }

    updateAudio();
    updateChannel();
    updateDiagnostics();
}

void Panel::updateAudio()
{
    const bool connected = m_dev.connected();
    const bool editable = connected && m_pwOk;
    m_rate->setEnabled(editable);
    m_buffer->setEnabled(editable);

    if (m_pwOk && !m_rate->view()->isVisible()) {
        m_rate->setCurrentIndex(m_rate->findData(m_pwRate));
    }

    if (m_pwOk && !m_buffer->view()->isVisible()) {
        QList<int> sizes;
        for (int s = 32; s <= 4096; s *= 2) {
            if ((m_pwMinQuantum == 0 || s >= m_pwMinQuantum)
                && (m_pwMaxQuantum == 0 || s <= m_pwMaxQuantum)) {
                sizes << s;
            }
        }

        if (m_pwQuantum > 0 && !sizes.contains(m_pwQuantum)) {
            sizes << m_pwQuantum;
            std::ranges::sort(sizes);
        }

        if (sizes != m_bufferSizes) {
            m_bufferSizes = sizes;
            m_buffer->clear();

            for (int s : sizes) {
                m_buffer->addItem(QString("%1 samples").arg(s), s);
            }
        }

        m_buffer->setCurrentIndex(m_buffer->findData(m_pwQuantum));
    }

    m_state->setEnabled(connected);
    m_latency->setEnabled(connected);
    QString state = "-";

    if (connected) {
        state = m_dev.streaming() ? "Streaming" : "Idle";
    }

    m_state->setText(state);

    double ms = -1;

    if (m_dev.streaming() && m_dev.delayFrames() >= 0 && m_dev.hwRate() > 0) {
        ms = 1000.0 * static_cast<double>(m_dev.delayFrames())
           / m_dev.hwRate(); // measured on the running stream
    } else if (m_pwOk && m_pwRate > 0 && m_pwQuantum > 0) {
        ms = 1000.0 * m_pwQuantum / m_pwRate; // what the buffer setting implies
    }

    m_latency->setText(connected && ms >= 0 ? QString("%1 ms").arg(ms, 0, 'f', 1) : "-");
}

void Panel::updateChannel()
{
    const bool connected = m_dev.connected();
    Notice *notice = m_notices[1];
    bool editable = false;

    if (m_startupMode) {
        editable = m_unlocked;
        notice->set(Notice::Kind::Info,
                    m_unlocked ? "Click Save to store these settings as the startup configuration."
                               : "Click the lock to make changes.");
        notice->showLock(true, !m_unlocked);
        notice->save->setVisible(m_unlocked);
    } else {
        editable = connected;
        notice->showLock(false);
        notice->save->setVisible(false);

        if (connected) {
            notice->set(
                Notice::Kind::Info,
                "Here you can change the Input Sensitivity and Direct Thru settings. Note that changes "
                "made on this tab will be lost after restarting. Please use the Startup tab to set up "
                "the default startup configuration.");
        } else {
            notice->set(Notice::Kind::Error, "Interface not connected.");
        }

        // Follow changes made elsewhere (alsamixer, the startup daemon ...).
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

    m_latStart->setText(m_latTest.running() ? "Stop" : "Start");
}

// ------------------------------------------------------------ Channel Setup

void Panel::setChannelMode(bool startup)
{
    m_startupMode = startup;
    m_unlocked = false;
    (startup ? m_tabStartup : m_tabCurrent)->setChecked(true);
    showPorts(startup ? loadStartup() : m_dev.ports());
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

void Panel::setPipewireSetting(const char *key, int value)
{
    if (m_dev.demo()) {
        return;
    }

    QProcess::startDetached("pw-metadata", {"-n", "settings", "0", key, QString::number(value)});
    QTimer::singleShot(300, this, &Panel::startDump);
}

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
    QHash<QString, int> settings;

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
        } else if (type.endsWith(":Metadata")
                   && o["props"].toObject()["metadata.name"].toString() == "settings") {
            for (const auto &m : o["metadata"].toArray()) {
                settings[m.toObject()["key"].toString()] = m.toObject()["value"].toVariant().toInt();
            }
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
    m_pwOk = settings.contains("clock.rate");
    const int forcedRate = settings.value("clock.force-rate");
    const int forcedQuantum = settings.value("clock.force-quantum");
    m_pwRate = forcedRate > 0 ? forcedRate : settings.value("clock.rate");
    m_pwQuantum = forcedQuantum > 0 ? forcedQuantum : settings.value("clock.quantum");
    m_pwMinQuantum = settings.value("clock.min-quantum");
    m_pwMaxQuantum = settings.value("clock.max-quantum");
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
