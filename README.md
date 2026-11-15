# Traktor Audio 6 Control Panel für Linux

Nachbau des Native-Instruments-Control-Panels für das Traktor Audio 6
(USB `17cc:1011`) in C++ / Qt 6. Aufbau, Maße und Beschriftungen folgen den
Screenshots des Windows-Panels aus dem offiziellen Handbuch (Kapitel 6.1).

## Bauen und starten

Benötigt: Qt 6 (Widgets), alsa-lib, CMake, ein C++20-Compiler.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/traktor-audio6-panel          # Panel öffnen
./build/traktor-audio6-panel --demo   # simuliertes Interface, fasst keine Hardware an
```

Installieren (Programm, Starter-Eintrag, systemd-User-Dienst):

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build && cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now traktor-audio6-startup.service   # für die Startup-Einstellungen
```

## Was die Seiten unter Linux tun

| Original | Umsetzung |
|---|---|
| **Channel Setup → Current** (Direct Thru, Phono/Line je Port A/B) | Direkt die vier ALSA-Schalter des Kernel-Treibers (`Direct Thru Channel A/B`, `Phono Input Channel A/B`). Änderungen von außen (z. B. `alsamixer`) erscheinen sofort. |
| **Channel Setup → Startup** (Schloss, Save) | Gespeichert in `~/.config/traktor-audio6/startup.ini` und bei jedem Einstecken auf das Gerät geschrieben – vom Dienst `traktor-audio6-startup` (`--daemon`) bzw. vom offenen Panel. |
| **Sample Rate / Process Buffer** | PipeWire: `clock.force-rate` / `clock.force-quantum`. |
| **Processing State** | `Idle` / `Streaming` aus dem ALSA-Streamstatus. |
| **Output Latency** | Bei laufender Wiedergabe der gemessene ALSA-Delay, sonst Puffergröße ÷ Samplerate. |
| **Buffer Underruns** | Xrun-Zähler der PipeWire-Knoten des Interfaces (`pw-top`). |
| **Number of Clients** | Anzahl der PipeWire-Knoten, die mit dem Interface verbunden sind. |
| **USB I/O Errors / USB Data Errors** | Kernel-Meldungen zum USB-Port des Interfaces seit Programmstart (`journalctl -k`). |
| **System Performance Test** | Statt DPC-Latenz: Verspätung eines 1-ms-Timers (Scheduling-Latenz) in µs. |
| **Driver / Firmware Version** | Kernel-Version (der Treiber `snd-usb-audio` ist Teil des Kernels) / `bcdDevice` des USB-Geräts. |

## Abweichungen vom Original

- **Startup-Einstellungen liegen nicht im Gerät.** Der Linux-Treiber kennt den
  Befehl für den geräteinternen Startup-Speicher nicht. Sie gelten daher nur an
  diesem Rechner; im Stand-alone-Betrieb (nur Netzteil) nutzt das Interface
  weiter, was zuletzt unter Windows/macOS im Gerät gespeichert wurde.
- **Sample Rate und Process Buffer gelten für ganz PipeWire**, nicht nur für
  das Interface, und werden bei einem PipeWire-Neustart zurückgesetzt.
- Ohne PipeWire bzw. ohne Leserecht auf das Kernel-Log zeigen die betroffenen
  Felder `-`. Der Zustand `Panic` existiert unter Linux nicht.
- Das Schloss auf dem Startup-Tab fragt kein Administrator-Passwort ab.
- Der Hinweistext auf dem Current-Tab verweist auf den „Startup tab“ (im
  Original steht dort fälschlich „USB and DC tabs“).
