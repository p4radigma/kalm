# Kalm

Universal native desktop application and CLI (**C++20 / Qt 6**) for controlling wireless TWS earbuds and headphones — active noise cancellation (ANC), transparency, battery levels, sound profiles, multipoint, and device settings — directly over Bluetooth RFCOMM on **Windows 10/11** and **Linux**, with no mobile app required.

> **Note:** This project was built with the assistance of AI and is intended primarily for personal use.

<p align="center">
  <img src="assets/screenshot.png" alt="Kalm Screenshot" width="520">
</p>

## Features
- **Multi-Brand Protocol Engine**: Auto-detects the connected earbuds' wire protocol (**vivo / iQOO Compact GAIA**, **HUAWEI SPP**, **Nothing / CMF SPP**, **realme / OPPO / OnePlus Link**) and dynamically adapts the UI and tray menus (`SessionCapabilities`) so only supported controls are shown.
- **Noise Control & ANC Levels**:
  - *Noise Cancelling (ANC)*, *Off*, and *Transparency* modes.
  - Per-model ANC intensity & environment presets (*Adaptive / Dynamic*, *Mild*, *Standard*, *Deep*, *Airplane*, *Metro*, *Bus*) and *Vocal enhancement* transparency where supported.
- **Hardware Telemetry & Controls**: Left / Right / Case battery & charging status, in-ear wear state, built-in EQ presets, low-latency Game Mode, Wearing Detection (with local MPRIS media auto-pause on Linux for HUAWEI), stem long-press cycle, Dual-Connect Multipoint host switching, and Earbud Finder.
- **Native Desktop UX & System Tray**:
  - **Linux**: Native **KDE Kirigami (`FormCard`)** interface with Wayland activation token support.
  - **Windows 10/11**: Dark **Fluent (`QtQuick Controls`)** interface with zero external runtime dependencies.
  - **System Tray & Instant CLI**: Mode-colored tray icon, middle-click ANC toggle, right-click quick menu, and sub-millisecond CLI commands via local IPC (`kalm toggle`, `kalm anc`, `kalm status`) ideal for global keyboard shortcuts and status bars (Waybar / Polybar).
- **Multi-Device Management**: Pair/scan multiple earbuds, set custom aliases, and switch manually or automatically when connected.
- **Built-in Multi-Vendor Protocol Probe**: 1-click privacy-safe RFCOMM diagnostic scanner (`kalm probe` / Drawer → *Diagnostic Report…*) for adding support for new wireless earbuds.
- **Localization**: **English** (default) and **Russian** built-in, switchable live in **Drawer (☰) → Language** or via CLI, with zero-rebuild JSON extensibility (see [Translations](docs/TRANSLATIONS.md)).

---

## Supported Devices

### Verified on Hardware

| Model | Protocol | Support Level |
|---|---|---|
| **vivo TWS 5 Hi-Fi** | vivo Compact GAIA (`0xFF`) | **Full support** — ANC modes, intensities & scene presets, Vocal transparency, EQ (*Ultimate Master Hi-Fi*), Game Mode, Wearing Detection, stem cycle, Multipoint, Find My Earbuds, L/R/Case battery & firmware |
| **HUAWEI FreeBuds 6i** | HUAWEI SPP (`0x5A`) | **Full support** — ANC / Transparency / Off, Dynamic / Mild / Standard / Deep ANC levels, L/R/Case battery & charging flags, EQ presets, Game Mode, Wearing Detection + MPRIS auto-pause, Dual-Connect Multipoint & firmware |
| **realme Buds T300** | realme / OPPO Link (`0xAA`) | **Noise control & battery** — ANC / Transparency / Off switching with live push sync, L/R/Case battery & firmware *(extended controls coming soon)* |

### Compatible Model Families

| Ecosystem / Protocol | Compatible Models & Scope |
|---|---|
| **HUAWEI SPP (`0x5A`, RFCOMM ch. 1)** | **HUAWEI FreeBuds Pro / Pro 2 / Pro 3 / Pro 4**, **FreeBuds 5 / 5i / 4 / SE 2**, **FreeLace / FreeLace Pro 2**, **HUAWEI Eyewear / WATCH Buds** — ANC modes & levels, battery, EQ, Game Mode, auto-pause, and Dual-Connect depending on model capabilities *(ported from GPL-3 [OpenFreebuds](https://github.com/melianmiko/OpenFreebuds))* |
| **vivo / iQOO GAIA v4 (`0xFF`)** | **vivo TWS 5 / 5 Pro / 5e / 4 / 4 Hi-Fi / 3 / 3 Pro**, **iQOO TWS 5 / 5e / 2 / 1 / 1e**, **vivo / iQOO Headphones** — full ANC, EQ, Game Mode, Multipoint & battery |
| **vivo / iQOO GAIA v3 (`0xFF`)** | **vivo TWS 3e**, **vivo TWS Air3 Pro**, **iQOO TWS Air3 Pro** — ANC, EQ, Game Mode, Multipoint & battery; non-ANC models (*vivo TWS 5i / 3i / Air3 / Air2*, *iQOO TWS 5i / 1i / Air3*) support battery, EQ & Multipoint |
| **Nothing / CMF SPP (`0x55`, UUID `aeac4a03-…`)** | **CMF Buds 2 Plus**, **CMF Buds 2 / 2a / Pro 2 / Buds / Buds Pro**, **CMF Neckband Pro / Headphone Pro**, **Nothing Ear (1) / (2) / (a) / (3) / (3a) / (open) / (stick)**, **Nothing Headphone (1)** — ANC modes & levels (*Adaptive / Low / Mid / High*), Transparency, L/R/Case battery & charging, in-ear wear state, EQ presets (*Dirac Opteo / Pop / Rock / Electronic / Vocals / Classical* & classic EQ), Low-Lag Game Mode, In-Ear Detection, Find My Earbuds & firmware *(awaiting live hardware verification on CMF Buds 2 Plus — [#4](https://github.com/p4radigma/kalm/issues/4))* |
| **realme / OPPO / OnePlus (`0xAA`)** | **realme Buds** (*Air / T-series*), **OPPO Enco**, and **OnePlus Buds** models using the `0xAA` RFCOMM link protocol — ANC / Transparency / Off switching, battery & firmware *(EQ, Game Mode & Multipoint planned)* |

*Using different wireless earbuds? Run `kalm probe` and follow the [New Earbuds Guide](CONTRIBUTING.md#requesting-support-for-new-earbuds)!*

---

## Quick Start & Running

**Windows 10 / 11 (x64 — no dependencies required):**
Download from [Releases](https://github.com/p4radigma/kalm/releases):
- **`Kalm-win64-setup.exe`** — single-file user installer (no admin rights required, optional Start Menu / Desktop / tray autostart), **or**
- **`Kalm-win64-portable.zip`** — portable archive: extract anywhere and run `kalm.exe`.

**Linux — Option 1 (Self-Contained AppImage, no Qt6/Kirigami dependencies required):**
Download `Kalm-x86_64.AppImage` from [Releases](https://github.com/p4radigma/kalm/releases) (only BlueZ/Bluetooth is needed on the host):
```bash
chmod +x Kalm-x86_64.AppImage
./Kalm-x86_64.AppImage
```

**Linux — Option 2 (Lightweight Native Binary ~770 KB, uses system Qt6 & Kirigami):**
Install runtime packages for your distro (see [Building & Dependencies](docs/BUILDING.md)), download `kalm-linux-x86_64` from [Releases](https://github.com/p4radigma/kalm/releases), and run:
```bash
chmod +x kalm-linux-x86_64
./kalm-linux-x86_64
```

**CLI Usage (`kalm` in `PATH`):**
```bash
kalm                                         # Launch GUI (or raise existing window)
kalm --minimized                             # Start minimized to system tray (ideal for autostart)
kalm status [--json]                         # Show battery, ANC mode/level, EQ, multipoint
kalm anc [adaptive|mild|standard|deep|plane|metro|bus] [--notify]
kalm trans [standard|voice] [--notify]
kalm off [--notify]
kalm toggle [--modes anc,trans] [--notify]   # Cycle modes (ideal for global shortcuts)
kalm eq standard                             # standard | vocal | bass | treble | audiobook | master
kalm game on | off                           # Low-latency game mode
kalm lang en | ru | auto                     # Persist interface language
kalm probe [-d <MAC|name>] [--listen 15]     # Privacy-safe protocol dump for new earbuds
kalm devices [list|scan|add <MAC> --alias <name>|use <name>|rename <name> <new>|remove <name>]
```

---

## Documentation

- **[Building & Dependencies](docs/BUILDING.md)** — Distro package commands (Fedora, Debian/Ubuntu, Arch, openSUSE, Void) and CMake build/packaging instructions for Linux & Windows.
- **[Translations](docs/TRANSLATIONS.md)** — How to switch languages or add a new language via JSON without recompiling.
- **[Contributing & Adding New Earbuds](CONTRIBUTING.md)** — How to capture a `kalm probe` report for unsupported earbuds, AI coding agents policy, and multi-vendor architecture guide.
- **[Credits & Acknowledgments](docs/CREDITS.md)** — Contributors, hardware testers, open-source protocol research references, and third-party libraries.

---

## License

Licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See [`LICENSE`](LICENSE) for the full license text.
