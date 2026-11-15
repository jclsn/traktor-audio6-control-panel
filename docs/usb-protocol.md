# USB protocol of the port settings

The Traktor Audio 6 (USB `17cc:1011`) takes its port settings as vendor
control requests to the device on endpoint 0.

| Action | bmRequestType | bRequest | wValue | wIndex | Data |
|---|---|---|---|---|---|
| Set Direct Thru | `0x40` | `0x01` | 0 = off, 1 = on | `(setting << 8) \| port` | none |
| Set input mode | `0x40` | `0x02` | 0 = Line, 1 = Phono | `(setting << 8) \| port` | none |
| Read either one | `0xC0` | `0x01` / `0x02` | 0 | as above | 1 byte |
| Save to flash | `0x40` | `0x10` | 0 | 0 | none |

- `port` is `0x03` for A and `0x05` for B.
- `setting` is `0` for Current and `1` for Startup, so the Startup values
  live at wIndex `0x0103` and `0x0105`.

The Current requests are the ones the kernel driver (`snd-usb-audio`) sends
for its mixer switches. The panel uses those switches for the Current
settings and sends the Startup requests itself through usbfs, which works
while the kernel driver is bound to the interface.

Startup values are only staged by the set requests. Reads keep returning the
stored values until the flash request has been sent.

## Verification

Checked against a device with firmware 47:

- Reading both settings.
- Writing Startup values followed by the flash request, after which they read
  back changed.

Input mode 1 is taken to mean Phono because the factory Startup default
(Phono with Direct Thru on) read back as 1.
