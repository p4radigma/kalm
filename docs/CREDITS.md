# Credits & Acknowledgments

[← Back to README](../README.md)

---

## Contributors & Hardware Verification

- **[@p4radigma](https://github.com/p4radigma)** — Project creator, core C++20/Qt6 architecture, vivo / iQOO Compact GAIA & realme / OPPO Link drivers, Windows Winsock2 RFCOMM port, and hardware verification on *vivo TWS 5 Hi-Fi*.
- **[@aboba1414](https://github.com/aboba1414)** — HUAWEI FreeBuds / FreeLace SPP protocol driver, dynamic `SessionCapabilities` UI/tray model, MPRIS wear auto-pause, Kirigami popup delegate fix, hardware verification on *HUAWEI FreeBuds 6i* ([#2](https://github.com/p4radigma/kalm/pull/2)), and AppImage `libselinux` bundling fix ([#3](https://github.com/p4radigma/kalm/pull/3)).
- **[@Gitveu](https://github.com/Gitveu)** — RFCOMM diagnostics and live hardware testing for *realme Buds T300* on Windows 11 ([#1](https://github.com/p4radigma/kalm/issues/1)).
- **[@akaremii26](https://github.com/akaremii26)** — RFCOMM diagnostics and feature specification for *CMF Buds 2 Plus* ([#4](https://github.com/p4radigma/kalm/issues/4)).
- **[@wlroots](https://github.com/wlroots)** — Hardware verification and testing for *CMF Buds 2* ([#5](https://github.com/p4radigma/kalm/issues/5)).
- **[@kreitsir0](https://github.com/kreitsir0)** — Hardware testing and RFCOMM diagnostics for *CMF Buds Pro 2* ([#6](https://github.com/p4radigma/kalm/issues/6)).

---

## Protocol Research & Open-Source References

Kalm's C++20 protocol codecs were built with reference to interoperability documentation, packet captures, and prior open-source community research:

- **[OpenFreebuds](https://github.com/melianmiko/OpenFreebuds)** (`GPL-3.0`, by [@melianmiko](https://github.com/melianmiko)) — HUAWEI FreeBuds / FreeLace `0x5A` SPP framing, CRC16-XMODEM, TLV command IDs, and model capability profiles.
- **[SwiftNothingEar](https://github.com/bestK1ngArthur/swift-nothing-ear)** (`GPL-3.0`, by [@bestK1ngArthur](https://github.com/bestK1ngArthur)), **[BudsLink](https://github.com/maniacx/BudsLink)** (`GPL-3.0`, by [@maniacx](https://github.com/maniacx)), **[nothing-x-macos](https://github.com/nikiomori/nothing-x-macos)** (`GPL-3.0`), and **[omarchy-nothing-ear](https://github.com/r-witz/omarchy-nothing-ear)** (`MIT`, by [@r-witz](https://github.com/r-witz)) — Nothing Ear / CMF Buds `0x55` SPP framing, CRC16-MODBUS, ANC/EQ/In-Ear/Find-My-Buds (`0x02`/`0x03`) command definitions, and RFCOMM channel discovery.
- **[Pods-Protocol-Reverse-Engineering](https://github.com/Star-ZER0/Pods-Protocol-Reverse-Engineering)** (`CC-BY-SA-4.0`, by [@Star-ZER0](https://github.com/Star-ZER0)) — Initial Bluetooth RFCOMM framing notes for vivo TWS earbuds.
- **[EncoDeck](https://github.com/TIANLI0/EncoDeck)** (`GPL-3.0`), **[vivopods-mac](https://github.com/FxRayHughes/vivopods-mac)** (`GPL-3.0`), **[TWS-Pods-PC](https://github.com/Zhaoyi-ya/TWS-Pods-PC)** (`GPL-3.0`), **[HyperEars](https://github.com/silverpoetry/HyperEars)** (`GPL-3.0`), **[vivo-tws-watch-control](https://github.com/hdzw1155/vivo-tws-watch-control)** (`MIT`), and **[ScrewVivoTWS](https://github.com/moculll/ScrewVivoTWS)** — Prior community work and cross-references on vivo Compact GAIA (`0xFF`) and OPPO / OnePlus / realme (`0xAA`) command families.
- Official vendor companion Android packages (*vivo Earphones*, *realme Link*, and *Nothing X*), inspected solely for clean-room Bluetooth RFCOMM interoperability (UUIDs, command/notification opcodes, and ANC/EQ preset enumerations).

---

## Libraries & Tooling

- **[Qt 6](https://www.qt.io/)** (`LGPL-3.0` / `GPL-3.0`) — Cross-platform application, QML/Quick UI, and D-Bus framework.
- **[KDE Frameworks 6 — Kirigami, Kirigami Addons & Breeze Icons](https://develop.kde.org/frameworks/kirigami/)** (`LGPL-2.1+` / `LGPL-3.0`) — Native Linux desktop UI components and iconography.
- **[BlueZ](https://www.bluez.org/)** & **Microsoft Winsock2 Bluetooth (`ws2_32` / `bthprops`)** — Native OS Bluetooth RFCOMM stacks on Linux and Windows.
- **[AppImageKit / appimagetool](https://github.com/AppImage/appimagetool)** (`MIT`) & **[Inno Setup](https://jrsoftware.org/isinfo.php)** — Self-contained Linux AppImage and Windows installer packaging.

---

## License

Kalm is distributed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See [`LICENSE`](../LICENSE) for the full text.
