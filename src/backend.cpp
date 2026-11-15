#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/usbdevice_fs.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

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

// Vendor requests of the interface for the port settings.
// wIndex is (setting << 8) | port; the kernel driver only ever uses setting 0.
constexpr quint8 kReqDirectThru = 0x01;
constexpr quint8 kReqInputMode = 0x02; // 0 = Line, 1 = Phono
constexpr quint8 kReqWriteFlash = 0x10;
constexpr quint16 kSettingStartup = 0x0100;
constexpr quint16 kPortA = 0x03;
constexpr quint16 kPortB = 0x05;

// Control transfers on endpoint 0 through usbfs. They need no interface claim,
// so they work while snd-usb-audio is bound to the device.
class UsbControl {
public:
    explicit UsbControl(const QString &node)
        : m_fd(open(node.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC))
    {
    }

    ~UsbControl()
    {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    UsbControl(const UsbControl &) = delete;
    UsbControl &operator=(const UsbControl &) = delete;
    UsbControl(UsbControl &&) = delete;
    UsbControl &operator=(UsbControl &&) = delete;

    bool read(quint8 request, quint16 index, bool &value) const
    {
        quint8 byte = 0;
        if (transfer(0xC0, request, 0, index, &byte, 1) != 1) {
            return false;
        }

        value = (byte & 1U) != 0;

        return true;
    }

    bool write(quint8 request, quint16 index, quint16 value) const
    {
        return transfer(0x40, request, value, index, nullptr, 0) >= 0;
    }

private:
    int transfer(quint8 type, quint8 request, quint16 value, quint16 index, void *data, quint16 length) const
    {
        if (m_fd < 0) {
            return -1;
        }

        usbdevfs_ctrltransfer t{};
        t.bRequestType = type;
        t.bRequest = request;
        t.wValue = value;
        t.wIndex = index;
        t.wLength = length;
        t.timeout = 1000; // ms
        t.data = data;

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): ioctl is the usbfs interface
        return ioctl(m_fd, USBDEVFS_CONTROL, &t);
    }

    int m_fd;
};

// Device node of the interface, e.g. /dev/bus/usb/001/051; empty if it is gone.
QString usbNode(int card)
{
    QDir dev(QFileInfo(QString("/sys/class/sound/card%1/device").arg(card)).canonicalFilePath());
    dev.cdUp();
    bool busOk = false;
    bool devOk = false;
    const int bus = readFile(dev.filePath("busnum")).trimmed().toInt(&busOk);
    const int num = readFile(dev.filePath("devnum")).trimmed().toInt(&devOk);

    if (!busOk || !devOk) {
        return {};
    }

    return QString("/dev/bus/usb/%1/%2").arg(bus, 3, 10, QChar('0')).arg(num, 3, 10, QChar('0'));
}

} // namespace

QString driverVersion()
{
    utsname u{};
    if (uname(&u) != 0) {
        return "-";
    }

    const auto m = QRegularExpression(R"(^\d+(\.\d+)*)").match(QString::fromLatin1(u.release));

    return m.hasMatch() ? m.captured(0) : QString::fromLatin1(u.release);
}

namespace {

// One "rtprio" line of the PAM limits configuration.
struct RtprioGrant {
    QString domain; // user name, "@group" or "*"
    int priority = 0;
    QString file;
};

QList<RtprioGrant> rtprioGrants(const QString &securityDir)
{
    QStringList files = {securityDir + "/limits.conf"};
    const QDir dropIns(securityDir + "/limits.d");

    for (const QString &name : dropIns.entryList({"*.conf"}, QDir::Files, QDir::Name)) {
        files << dropIns.filePath(name);
    }

    QList<RtprioGrant> grants;

    for (const QString &file : std::as_const(files)) {
        for (const QString &line : readFile(file).split('\n')) {
            // <domain> <type> <item> <value>, with "#" starting a comment
            const QStringList field = line.section('#', 0, 0).simplified().split(' ', Qt::SkipEmptyParts);

            if (field.size() == 4 && field[2] == "rtprio" && field[3].toInt() > 0) {
                grants.append({.domain = field[0], .priority = field[3].toInt(), .file = file});
            }
        }
    }

    return grants;
}

// Groups of the user according to the group database. Unlike the groups of
// the running session, this includes memberships that await the next login.
QStringList groupsOf(const passwd &user)
{
    int count = 64;
    std::vector<gid_t> ids(static_cast<std::size_t>(count));

    if (getgrouplist(user.pw_name, user.pw_gid, ids.data(), &count) < 0) {
        ids.resize(static_cast<std::size_t>(count));
        getgrouplist(user.pw_name, user.pw_gid, ids.data(), &count);
    }

    ids.resize(static_cast<std::size_t>(qMax(0, count)));
    QStringList names;

    for (const gid_t id : ids) {
        if (const group *entry = getgrgid(id)) { // NOLINT(concurrency-mt-unsafe)
            names << QString::fromLocal8Bit(entry->gr_name);
        }
    }

    return names;
}

bool groupExists(const QString &name)
{
    return getgrnam(name.toLocal8Bit().constData()) != nullptr; // NOLINT(concurrency-mt-unsafe)
}

} // namespace

int rtprioLimit()
{
    rlimit limit{};

    if (getrlimit(RLIMIT_RTPRIO, &limit) != 0) {
        return 0;
    }

    return static_cast<int>(qMin<rlim_t>(limit.rlim_cur, 99));
}

RtprioSetup rtprioSetup(const QString &securityDir)
{
    const passwd *user = getpwuid(getuid()); // NOLINT(concurrency-mt-unsafe)
    const QString name = user ? QString::fromLocal8Bit(user->pw_name) : QString::number(getuid());
    const QStringList groups = user ? groupsOf(*user) : QStringList();
    const QList<RtprioGrant> grants = rtprioGrants(securityDir);
    RtprioSetup setup;

    // Already covered, by name, by a wildcard or through a group?
    for (const RtprioGrant &grant : grants) {
        if (grant.domain == name || grant.domain == "*"
            || (grant.domain.startsWith('@') && groups.contains(grant.domain.mid(1)))) {
            setup.action = RtprioSetup::Action::LogInAgain;
            return setup;
        }
    }

    // A group that already has the limit: join it instead of adding an entry.
    // "realtime" is the group of Arch's realtime-privileges package, "audio"
    // the one other distributions use.
    const RtprioGrant *group = nullptr;

    for (const char *preferred : {"@realtime", "@audio", "@"}) {
        for (const RtprioGrant &grant : grants) {
            if (!group && grant.domain.startsWith(preferred) && groupExists(grant.domain.mid(1))) {
                group = &grant;
            }
        }
    }

    if (group) {
        const QString groupName = group->domain.mid(1);
        setup.action = RtprioSetup::Action::JoinGroup;
        setup.question = QString("Add the user '%1' to the group '%2'?").arg(name, groupName);
        setup.detail = QString("%1 already grants realtime priority %2 to that group, so no further "
                               "entry is needed.")
                           .arg(group->file)
                           .arg(group->priority);
        setup.command = {"usermod", "-a", "-G", groupName, name};
        setup.done = QString("The user '%1' is now a member of the group '%2'.").arg(name, groupName);

        return setup;
    }

    // 95 leaves the priorities above it to kernel threads such as IRQ handlers.
    const QString file = securityDir + "/limits.d/95-audio-realtime.conf";
    const QString contents = QString("# Realtime scheduling for audio applications\n"
                                     "%1 - rtprio 95\n"
                                     "%1 - memlock unlimited\n")
                                 .arg(name);
    setup.question = QString("Allow realtime priority 95 and locked memory for the user '%1'?").arg(name);
    setup.detail = QString("This writes %1.").arg(file);
    setup.command = {
        "sh", "-c",     R"sh(mkdir -p "$(dirname "$2")" && printf '%s' "$1" > "$2" && chmod 644 "$2")sh",
        "sh", contents, file,
    };
    setup.done = QString("%1 has been written.").arg(file);

    return setup;
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

bool Device::readStartup(PortSettings &s) const
{
    if (m_demo) {
        s = m_demoStartup;
        return true;
    }

    if (!m_connected) {
        return false;
    }

    const UsbControl usb(usbNode(m_card));

    return usb.read(kReqDirectThru, kSettingStartup | kPortA, s.directA)
        && usb.read(kReqDirectThru, kSettingStartup | kPortB, s.directB)
        && usb.read(kReqInputMode, kSettingStartup | kPortA, s.phonoA)
        && usb.read(kReqInputMode, kSettingStartup | kPortB, s.phonoB);
}

bool Device::writeStartup(const PortSettings &s)
{
    if (m_demo) {
        m_demoStartup = s;
        return true;
    }

    if (!m_connected) {
        return false;
    }

    // The values are only staged in the interface until the flash request.
    const UsbControl usb(usbNode(m_card));
    PortSettings stored;

    return usb.write(kReqDirectThru, kSettingStartup | kPortA, s.directA)
        && usb.write(kReqDirectThru, kSettingStartup | kPortB, s.directB)
        && usb.write(kReqInputMode, kSettingStartup | kPortA, s.phonoA)
        && usb.write(kReqInputMode, kSettingStartup | kPortB, s.phonoB) && usb.write(kReqWriteFlash, 0, 0)
        && readStartup(stored) && stored == s;
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
