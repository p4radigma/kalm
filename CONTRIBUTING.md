# Contributing to Kalm

[← Back to README](README.md)

Contributions, bug fixes, [translations](docs/TRANSLATIONS.md), and new wireless earbud protocols/profiles are welcome!

---

## AI Coding Agents Policy
- **AI coding agents and LLM-assisted PRs are explicitly allowed and welcome.**
- Please ensure every PR:
  - Compiles cleanly without warnings (`cmake --build build`, see [Building & Dependencies](docs/BUILDING.md)).
  - Passes all unit & handle/FD leak tests (`ctest --test-dir build --output-on-failure`).
  - Works cross-platform on both **Linux** and **Windows** (use `UniqueSocket`, `pollSocketRead`, `recvSocket`, `sendAll` in `src/transport.hpp` rather than raw OS socket descriptors).
  - Clearly notes whether protocol changes were verified on real hardware or inferred.

---

## Opening Issues

Use the repository Issue Templates:
- **Bug Report** — include your OS / Linux distro, Kalm version, earbud model, and output of `kalm status` / `kalm probe`.
- **New Wireless Earbuds Support** — follow the diagnostic steps below so we have everything needed to add your earbuds quickly.

---

## Requesting Support for New Earbuds

1. Pair and connect your earbuds via Bluetooth, and **take both earbuds out of the charging case**.
2. Collect a read-only diagnostic dump (personal MAC bytes, serial numbers, and paired host names are automatically masked):
   - **From GUI:** open **Drawer (☰) → Diagnostic Report… → Copy Report to Clipboard**.
   - **From terminal:** run `kalm probe` (or `kalm probe -d "Your Earbuds Name"`).
3. *(Optional, very helpful for unknown protocols)* Run live event capture for 15 seconds while toggling ANC / Transparency / Off on the earbuds (or in the mobile app connected via multipoint):
   ```bash
   kalm probe --listen 15
   ```
4. Open a **New Wireless Earbuds Support** issue and paste the output along with the list of features available in the official mobile app.

---

## Adding New Protocols & Profiles (Developer Guide)

- **Protocol codecs & state mapping**:
  - `src/protocol.*` — vivo Compact GAIA (`0xFF`) & realme / OPPO / OnePlus Link (`0xAA`).
  - `src/huawei.*` — HUAWEI FreeBuds / FreeLace SPP (`0x5A`, CRC16-XMODEM).
  - `src/nothing.*` — Nothing Ear / CMF Buds SPP (`0x55`, CRC16-MODBUS).
  - Add new vendor codecs in a dedicated `src/<vendor>.{hpp,cpp}` module with unit tests in `tests/test_kalm.cpp`.
- **Transport & auto-detection**:
  - Add UUID / Bluetooth name detection and RFCOMM channel handshake in `src/transport_common.cpp` (`openEarbudRfcomm`) and read-only probing in `src/probe.cpp`.
- **UI & System Tray capabilities**:
  - Populate `SessionCapabilities` in `src/session.*` so both Linux (`qml/Main.qml`), Windows (`qml/MainWin.qml`), and the system tray (`KalmTrayController`) automatically show supported controls and hide unsupported ones.
- **vivo / iQOO GAIA model overrides**:
  - Wire-level profile overrides can be tested without recompiling via `~/.config/kalm/models.json` (matching [`models.json`](models.json)).
- **Credits & Licensing**:
  - All contributions and protocol references must be compatible with **GNU GPLv3 (`GPL-3.0-or-later`)** ([`LICENSE`](LICENSE)).
  - Whenever adding a new protocol reference, contributor, or hardware tester, please update [`docs/CREDITS.md`](docs/CREDITS.md).
