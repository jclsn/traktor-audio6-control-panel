# Traktor Audio 6 Control Panel for Linux

Re-creation of the Native Instruments control panel for the Traktor Audio 6
(USB `17cc:1011`) in C++ / Qt 6. Layout, dimensions and labels follow the
screenshots of the Windows panel in the official manual (chapter 6.1).

## Build and run

Requires Qt 6 (Widgets), alsa-lib, CMake and a C++20 compiler.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/traktor-audio6-panel          # open the panel
./build/traktor-audio6-panel --demo   # simulated interface, does not touch hardware
```

Install (program, launcher entry, systemd user service):

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build && cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now traktor-audio6-startup.service   # for the Startup settings
```

## What the pages do on Linux

| Original | Implementation |
|---|---|
| **Channel Setup → Current** (Direct Thru, Phono/Line per port A/B) | The four ALSA switches of the kernel driver (`Direct Thru Channel A/B`, `Phono Input Channel A/B`). Changes made elsewhere (e.g. `alsamixer`) show up immediately. |
| **Channel Setup → Startup** (lock, Save) | Stored in `~/.config/traktor-audio6/startup.ini` and written to the device every time it is plugged in, by the `traktor-audio6-startup` service (`--daemon`) or by the open panel. |
| **Sample Rate / Process Buffer** | PipeWire: `clock.force-rate` / `clock.force-quantum`. |
| **Processing State** | `Idle` / `Streaming` from the ALSA stream status. |
| **Output Latency** | The measured ALSA delay while playback is running, otherwise buffer size ÷ sample rate. |
| **Buffer Underruns** | Xrun counters of the interface's PipeWire nodes (`pw-top`). |
| **Number of Clients** | Number of PipeWire nodes linked to the interface. |
| **USB I/O Errors / USB Data Errors** | Kernel messages for the interface's USB port since program start (`journalctl -k`). |
| **System Performance Test** | Instead of DPC latency: how late a 1 ms timer wakes up (scheduling latency), in µs. |
| **Driver / Firmware Version** | Kernel version (the `snd-usb-audio` driver is part of the kernel) / `bcdDevice` of the USB device. |

## Differences from the original

- **Startup settings are not stored in the device yet.** The kernel driver
  only exposes the Current settings, so the panel keeps the Startup settings
  in a local file. They therefore apply on this computer only; in stand-alone
  operation (power supply only) the interface keeps using whatever was last
  saved to it on Windows/macOS. The requests needed to store them in the
  device are documented below but not implemented.
- **Sample Rate and Process Buffer apply to all of PipeWire**, not just the
  interface, and are reset when PipeWire restarts.
- Without PipeWire, or without read access to the kernel log, the affected
  fields show `-`. The `Panic` state does not exist on Linux.
- The lock on the Startup tab does not ask for an administrator password.
- The hint on the Current tab refers to the "Startup tab" (the original
  mistakenly says "USB and DC tabs").

## USB protocol of the port settings

Taken from the disassembly of the original macOS Control Panel 3.1.0
(`NI::DRV::AudioClient::Device` and `NI::NHL2::XMOSDevice`). All settings are
vendor control requests to the device on endpoint 0:

| Action | bmRequestType | bRequest | wValue | wIndex | Data |
|---|---|---|---|---|---|
| Set Direct Thru | `0x40` | `0x01` | 0 = off, 1 = on | `(setting << 8) \| port` | none |
| Set input mode | `0x40` | `0x02` | 0 = Line, 1 = Phono | `(setting << 8) \| port` | none |
| Read either one | `0xC0` | `0x01` / `0x02` | 0 | as above | 1 byte |
| Save to flash | `0x40` | `0x10` | 0 | 0 | none |

- `port` is `0x03` for A and `0x05` for B.
- `setting` is `0` for Current and `1` for Startup, so the Startup values
  live at wIndex `0x0103` and `0x0105`.

The Current requests are the ones the kernel driver already sends. On the
Startup tab the original panel sends each change right away with `setting` 1,
and Save then issues the single flash request.

Verification status: the read requests were tried against a real device for
both settings and returned plausible values (Startup held the factory default,
Phono with Direct Thru on, which is also why input mode 1 is taken to mean
Phono). The Startup writes and the flash request are read from the code only
and have not been sent to a device.
