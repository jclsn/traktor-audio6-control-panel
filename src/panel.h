#pragma once
#include "backend.h"
#include "widgets.h"

#include <QMainWindow>
#include <QProcess>
#include <QSet>

#include <atomic>
#include <thread>

// Linux counterpart of the DPC latency test: measures how late a 1 ms timer
// wake-up arrives (scheduling latency), in microseconds.
class LatencyTest {
public:
    LatencyTest() = default;

    ~LatencyTest() { stop(); }

    LatencyTest(const LatencyTest &) = delete;
    LatencyTest &operator=(const LatencyTest &) = delete;
    LatencyTest(LatencyTest &&) = delete;
    LatencyTest &operator=(LatencyTest &&) = delete;

    void start();
    void stop();

    void reset() { m_max = 0; }

    bool running() const { return m_run; }

    bool hasData() const { return m_current >= 0; }

    long currentUs() const { return m_current; }

    long maxUs() const { return m_max; }

private:
    std::thread m_thread;
    std::atomic<bool> m_run{false};
    std::atomic<long> m_current{-1};
    std::atomic<long> m_max{0};
};

class Panel : public QMainWindow {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Panel)

public:
    explicit Panel(bool demo);
    ~Panel() override;
    void saveScreenshots(const QString &dir);

private:
    void buildAudioPage();
    void buildChannelPage();
    void buildDiagnosticsPage();
    void selectPage(int index);

    void poll(); // 500 ms: re-read the device and update every display
    void updateAudio();
    void updateChannel();
    void updateDiagnostics();

    // Channel Setup
    void setChannelMode(bool startup);
    void showPorts(const PortSettings &s);
    PortSettings shownPorts() const;
    void portEdited();

    // PipeWire / kernel log monitoring
    void startDump();
    void parseDump(const QByteArray &json);
    void startTop();
    void parseTop(const QByteArray &text);
    void readJournal();
    void setPipewireSetting(const char *key, int value);

    Device m_dev;
    ui::Canvas *m_canvas = nullptr;
    QList<ui::SideTab *> m_tabs;
    QList<QWidget *> m_pages;
    QList<ui::Notice *> m_notices; // one per page
    int m_page = 0;
    bool m_firstPoll = true;
    bool m_wasConnected = false;

    // Audio Settings
    ui::Combo *m_rate = nullptr;
    ui::Combo *m_buffer = nullptr;
    ui::ValueBox *m_state = nullptr;
    ui::ValueBox *m_latency = nullptr;
    QList<int> m_bufferSizes;

    // Channel Setup
    ui::SubTab *m_tabCurrent = nullptr;
    ui::SubTab *m_tabStartup = nullptr;
    ui::Check *m_direct[2] = {};
    ui::Radio *m_phono[2] = {};
    ui::Radio *m_line[2] = {};
    bool m_startupMode = false;
    bool m_unlocked = false;

    // Diagnostics
    ui::ValueBox *m_usbIoBox = nullptr;
    ui::ValueBox *m_underrunBox = nullptr;
    ui::ValueBox *m_clientsBox = nullptr;
    ui::ValueBox *m_usbDataBox = nullptr;
    ui::ValueBox *m_latCurrent = nullptr;
    ui::ValueBox *m_latMax = nullptr;
    ui::ValueBox *m_driverBox = nullptr;
    ui::ValueBox *m_firmwareBox = nullptr;
    ui::Button *m_latStart = nullptr;
    LatencyTest m_latTest;

    // PipeWire state
    QProcess m_dump;
    QProcess m_top;
    QProcess m_journal;
    bool m_pwOk = false;
    bool m_journalOk = false;
    int m_pwRate = 0;
    int m_pwQuantum = 0;
    int m_pwMinQuantum = 0;
    int m_pwMaxQuantum = 0;
    int m_clients = 0;
    int m_xruns = 0;
    int m_xrunBase = 0;
    int m_usbIo = 0;
    int m_usbData = 0;
    QSet<QString> m_nodeNames;
};
