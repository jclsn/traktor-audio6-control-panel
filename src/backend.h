// Hardware access for the Traktor Audio 6 (Qt Core, libasound and usbfs; no GUI).
#pragma once
#include <QString>
#include <QStringList>

#include <cstdint>

struct PortSettings {
    // Factory default according to the manual: Phono + Direct Thru on both ports.
    bool directA = true, directB = true, phonoA = true, phonoB = true;
    bool operator==(const PortSettings &) const = default;
};

// The driver (snd-usb-audio) is part of the kernel, so this is the kernel version.
QString driverVersion();

// Highest realtime priority this session may give a thread (RLIMIT_RTPRIO);
// 0 means audio applications cannot use realtime scheduling on their own.
int rtprioLimit();

// What has to happen so that the user's sessions get a realtime priority
// limit. Existing grants in the PAM limits configuration are respected, so
// that no second entry is written next to, say, the realtime-privileges
// package's one for the "realtime" group.
struct RtprioSetup {
    enum class Action : std::uint8_t {
        WriteLimits, // nothing grants it yet: write a limits.d file for the user
        JoinGroup, // a group already has it: add the user to that group
        LogInAgain, // the user is already covered; only a new login is missing
    };

    Action action = Action::WriteLimits;
    QString question; // what the user is asked to agree to
    QString detail; // what agreeing does
    QStringList command; // to be run with administrator rights
    QString done; // confirmation once the command has succeeded
};

// securityDir is where limits.conf and limits.d live.
RtprioSetup rtprioSetup(const QString &securityDir = QStringLiteral("/etc/security"));

// ALSA card index of the interface, or -1 if it is not plugged in.
int findCard();

class Device {
public:
    explicit Device(bool demo = false);

    void refresh(); // re-read everything; call periodically
    bool setPorts(const PortSettings &s); // write the "Current" settings

    // The "Startup" settings live in the interface's flash memory and are what
    // it powers up with, also without a computer.
    bool readStartup(PortSettings &s) const;
    bool writeStartup(const PortSettings &s);

    bool demo() const { return m_demo; }

    bool connected() const { return m_connected; }

    int card() const { return m_card; }

    const PortSettings &ports() const { return m_ports; }

    bool streaming() const { return m_streaming; }

    int hwRate() const { return m_hwRate; } // rate of the open stream, 0 if none

    long delayFrames() const { return m_delayFrames; } // playback delay while running, else -1

    QString firmware() const { return m_firmware; }

    QString usbPort() const { return m_usbPort; } // e.g. "3-1"

private:
    void readUsbInfo();
    void readStreamState();

    bool m_demo;
    bool m_connected = false;
    int m_card = -1;
    PortSettings m_ports;
    PortSettings m_demoStartup;
    bool m_streaming = false;
    int m_hwRate = 0;
    long m_delayFrames = -1;
    QString m_firmware = "-";
    QString m_usbPort;
};
