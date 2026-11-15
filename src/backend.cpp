#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

#include <alsa/asoundlib.h>
#include <sys/utsname.h>

#include <chrono>
#include <thread>

namespace {

const char *const kUsbId = "17cc:1011";
const char *const kDirectA = "Direct Thru Channel A";
const char *const kDirectB = "Direct Thru Channel B";
const char *const kPhonoA = "Phono Input Channel A";
const char *const kPhonoB = "Phono Input Channel B";

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }

    return QString::fromUtf8(f.readAll());
}

// Read or write one boolean mixer switch, addressed by name.
bool ctlAccess(int card, const char *name, bool write, bool &value)
{
    snd_ctl_t *ctl = nullptr;
    const QByteArray dev = "hw:" + QByteArray::number(card);

    if (snd_ctl_open(&ctl, dev.constData(), write ? 0 : SND_CTL_READONLY) < 0) {
        return false;
    }

    snd_ctl_elem_value_t *v = nullptr;
    snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_interface(v, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(v, name);
    int rc = 0;

    if (write) {
        snd_ctl_elem_value_set_boolean(v, 0, value);
        rc = snd_ctl_elem_write(ctl, v);
    } else {
        rc = snd_ctl_elem_read(ctl, v);
        if (rc >= 0) {
            value = snd_ctl_elem_value_get_boolean(v, 0);
        }
    }

    snd_ctl_close(ctl);

    return rc >= 0;
}

QSettings startupStore()
{
    // NOLINTNEXTLINE(modernize-return-braced-init-list): the constructor is explicit
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "traktor-audio6", "startup");
}

} // namespace

PortSettings loadStartup()
{
    const QSettings s = startupStore();
    const PortSettings d;

    return {
        .directA = s.value("A/direct_thru", d.directA).toBool(),
        .directB = s.value("B/direct_thru", d.directB).toBool(),
        .phonoA = s.value("A/phono", d.phonoA).toBool(),
        .phonoB = s.value("B/phono", d.phonoB).toBool(),
    };
}

bool saveStartup(const PortSettings &p)
{
    QSettings s = startupStore();
    s.setValue("A/direct_thru", p.directA);
    s.setValue("A/phono", p.phonoA);
    s.setValue("B/direct_thru", p.directB);
    s.setValue("B/phono", p.phonoB);
    s.sync();

    return s.status() == QSettings::NoError;
}

QString driverVersion()
{
    utsname u{};
    if (uname(&u) != 0) {
        return "-";
    }

    const auto m = QRegularExpression(R"(^\d+(\.\d+)*)").match(QString::fromLatin1(u.release));

    return m.hasMatch() ? m.captured(0) : QString::fromLatin1(u.release);
}

int findCard()
{
    const QDir proc("/proc/asound");
    for (const QString &name : proc.entryList({"card[0-9]*"}, QDir::Dirs)) {
        if (readFile(proc.filePath(name + "/usbid")).trimmed().toLower() == kUsbId) {
            return name.mid(4).toInt();
        }
    }

    return -1;
}

Device::Device(bool demo)
    : m_demo(demo)
{
    if (demo) {
        m_connected = true;
        m_card = 0;
        m_firmware = "3";
    }
}

void Device::refresh()
{
    if (m_demo) {
        return;
    }

    const int card = findCard();

    if (card < 0) {
        m_connected = false;
        m_card = -1;
        m_streaming = false;
        m_hwRate = 0;
        m_delayFrames = -1;

        return;
    }

    if (card != m_card || !m_connected) {
        m_card = card;
        readUsbInfo();
    }

    PortSettings p;
    const bool ok = ctlAccess(card, kDirectA, false, p.directA) && ctlAccess(card, kDirectB, false, p.directB)
                 && ctlAccess(card, kPhonoA, false, p.phonoA) && ctlAccess(card, kPhonoB, false, p.phonoB);
    m_connected = ok; // unplugged between the two steps, or controls missing

    if (ok) {
        m_ports = p;
    }

    readStreamState();
}

bool Device::setPorts(const PortSettings &s)
{
    if (m_demo) {
        m_ports = s;
        return true;
    }

    if (!m_connected) {
        return false;
    }

    PortSettings w = s;
    // Try every switch even if one of them fails.
    bool ok = ctlAccess(m_card, kDirectA, true, w.directA);
    ok = ctlAccess(m_card, kDirectB, true, w.directB) && ok;
    ok = ctlAccess(m_card, kPhonoA, true, w.phonoA) && ok;
    ok = ctlAccess(m_card, kPhonoB, true, w.phonoB) && ok;
    refresh();

    return ok;
}

void Device::readUsbInfo()
{
    // /sys/class/sound/cardN/device -> .../<bus-port>/<bus-port>:1.0
    QDir dev(QFileInfo(QString("/sys/class/sound/card%1/device").arg(m_card)).canonicalFilePath());
    dev.cdUp();
    m_usbPort = dev.dirName();
    bool ok = false;
    const int bcd = readFile(dev.filePath("bcdDevice")).trimmed().toInt(&ok, 16);
    m_firmware = ok ? QString::number(bcd) : QString("-");
}

void Device::readStreamState()
{
    const QString base = QString("/proc/asound/card%1/").arg(m_card);
    const QString play = readFile(base + "pcm0p/sub0/status");
    const QString capt = readFile(base + "pcm0c/sub0/status");
    const bool playing = play.contains("RUNNING");
    m_streaming = playing || capt.contains("RUNNING");

    m_hwRate = 0;
    static const QRegularExpression rateRe(R"(^rate:\s*(\d+))", QRegularExpression::MultilineOption);

    for (const char *pcm : {"pcm0p", "pcm0c"}) {
        const auto m = rateRe.match(readFile(base + pcm + "/sub0/hw_params"));
        if (m.hasMatch()) {
            m_hwRate = m.captured(1).toInt();
            break;
        }
    }

    m_delayFrames = -1;
    static const QRegularExpression delayRe(R"(^delay\s*:\s*(-?\d+))", QRegularExpression::MultilineOption);
    const auto m = delayRe.match(play);

    if (playing && m.hasMatch()) {
        m_delayFrames = qMax(0LL, m.captured(1).toLongLong());
    }
}

int runDaemon()
{
    using namespace std::chrono_literals;
    QTextStream out(stdout);
    Device dev;
    bool present = false;

    for (;;) {
        bool now = findCard() >= 0;
        if (now && !present) {
            std::this_thread::sleep_for(1500ms); // let udev/alsactl restore mixer state first
            dev.refresh();

            if (dev.connected()) {
                const bool ok = dev.setPorts(loadStartup());
                out << "Traktor Audio 6 connected, startup settings "
                    << (ok ? "applied" : "could NOT be applied") << Qt::endl;
            } else {
                now = false; // retry on the next round
            }
        }

        present = now;
        std::this_thread::sleep_for(1s);
    }

    return 0;
}
