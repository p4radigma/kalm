# Kalm

Universal native desktop application and CLI (**C++20 / Qt 6**) for controlling wireless TWS earbuds and headphones — active noise cancellation (ANC), transparency, battery levels, sound profiles, multipoint, and device settings — directly over Bluetooth RFCOMM on **Windows 10/11** and **Linux**, with no mobile app required.

> **Note:** This project was built with the assistance of AI and is intended primarily for personal use.

<p align="center">
  <img src="assets/screenshot.png" alt="Kalm Screenshot" width="520">
</p>

## Features
- **Multi-Brand Protocol Engine**: Auto-detects the connected earbuds' wire protocol (**vivo / iQOO Compact GAIA**, **HUAWEI SPP**, **realme / OPPO / OnePlus Link**) and dynamically adapts the UI and tray menus (`SessionCapabilities`) so only supported controls are shown.
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
- **Localization**: **English** (default) and **Russian** built-in, switchable live in **Drawer (☰) → Language** or via CLI, with zero-rebuild JSON extensibility.

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
| **realme / OPPO / OnePlus (`0xAA`)** | **realme Buds** (*Air / T-series*), **OPPO Enco**, and **OnePlus Buds** models using the `0xAA` RFCOMM link protocol — ANC / Transparency / Off switching, battery & firmware *(EQ, Game Mode & Multipoint planned)* |

*Using different wireless earbuds? Run `kalm probe` and open a [New Wireless Earbuds Support](#requesting-support-for-new-earbuds-what-users-need-to-do) issue!*

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
Install runtime packages for your distro (see [Dependencies](#dependencies)), download `kalm-linux-x86_64` from [Releases](https://github.com/p4radigma/kalm/releases), and run:
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

## Dependencies (Linux Native Build)

| Scope | Packages / Components |
|---|---|
| **Build** | `cmake >= 3.20`, C++20 compiler (`g++ >= 11` or `clang++ >= 14`), `Qt6` headers (`Core`, `Gui`, `Widgets`, `DBus`, `Qml`, `Quick`) |
| **Runtime (Required)** | `BlueZ` + `D-Bus`, `Qt 6.5+` (`Base`, `Declarative/Quick`), `KDE Kirigami 6` (`org.kde.kirigami`), `Kirigami Addons` (`formcard`, `delegates`) |
| **Runtime (Recommended)** | `qqc2-desktop-style` & `breeze-icons` (native KDE styling), `libnotify` / `notify-send` (for `--notify`) |

### Installing Dependencies by Distro

**Fedora:**
```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel \
  kf6-kirigami kf6-kirigami-addons kf6-qqc2-desktop-style kf6-breeze-icons bluez libnotify
```

**Debian (13+) / Ubuntu (24.10+):**
```bash
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-org-kde-kirigami qml6-module-org-kde-kirigamiaddons-formcard \
  qml6-module-org-kde-kirigamiaddons-delegates qml6-module-org-kde-desktop \
  breeze-icon-theme bluez libnotify-bin
```

**Arch Linux / Manjaro / EndeavourOS:**
```bash
sudo pacman -S --needed cmake gcc qt6-base qt6-declarative \
  kirigami kirigami-addons qqc2-desktop-style breeze-icons bluez libnotify
```

**openSUSE (Tumbleweed / Leap 16):**
```bash
sudo zypper install cmake gcc-c++ qt6-base-devel qt6-declarative-devel \
  kf6-kirigami kf6-kirigami-addons kf6-qqc2-desktop-style breeze6-icons bluez libnotify-tools
```

**Void Linux:**
```bash
sudo xbps-install -S cmake gcc qt6-base-devel qt6-declarative-devel \
  kf6-kirigami kf6-kirigami-addons kf6-qqc2-desktop-style breeze-icons bluez libnotify
```

---

## Build & Install

**Windows (Visual Studio 2022 + Qt 6.8+ + CMake/Ninja):**
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
windeployqt --release --qmldir qml build/kalm.exe
```

**Linux (All Distros):**
```bash
git clone https://github.com/p4radigma/kalm.git
cd kalm
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```
*(User-local install without root: `cmake --install build --prefix ~/.local`)*

---

## Adding Translations

`translations/en.json` and `translations/ru.json` are embedded into the binary. To add a new language (e.g. `de`) **without recompiling**:
1. Copy `translations/en.json` to `~/.local/share/kalm/translations/de.json` on Linux (or `translations/de.json` next to `kalm.exe` on Windows) and set `"lang_name"` (e.g. `"Deutsch"`).
2. Translate the values (untranslated keys automatically fall back to `en.json`).
3. Select it in **Drawer (☰) → Language** or run `kalm lang de`.

---

## Contributing

Contributions, bug fixes, translations, and new wireless earbud protocols/profiles are welcome!

### AI Coding Agents Policy
- **AI coding agents and LLM-assisted PRs are explicitly allowed and welcome.**
- Please ensure every PR compiles cleanly (`cmake --build build`), passes all unit tests (`ctest --test-dir build --output-on-failure`), stays focused on a single concern, and clearly notes whether protocol changes were verified on real hardware or inferred.

### How to Open Issues
Use the repository Issue Templates:
- **Bug Report** — include your OS / Linux distro, Kalm version, earbud model, and output of `kalm status` / `kalm probe`.
- **New Wireless Earbuds Support** — follow the steps below so we have everything needed to add your earbuds quickly.

### Requesting Support for New Earbuds (What Users Need to Do)
1. Pair and connect your earbuds via Bluetooth, and **take both earbuds out of the charging case**.
2. Collect a read-only diagnostic dump (personal MAC bytes, serial numbers, and paired host names are automatically masked):
   - **From GUI:** open **Drawer (☰) → Diagnostic Report… → Copy Report to Clipboard**.
   - **From terminal:** run `kalm probe` (or `kalm probe -d "Your Earbuds Name"`).
3. *(Optional, very helpful for unknown protocols)* Run live event capture for 15 seconds while toggling ANC / Transparency / Off on the earbuds (or in the mobile app connected via multipoint):
   ```bash
   kalm probe --listen 15
   ```
4. Open a **New Wireless Earbuds Support** issue and paste the output along with the list of features available in the official mobile app.

### Adding New Protocols & Profiles (Contributor Guide)
- **Protocol codecs & state mapping**: `src/protocol.*` (vivo GAIA `0xFF` & realme/OPPO Link `0xAA`) and `src/huawei.*` (HUAWEI SPP `0x5A`).
- **Transport & auto-detection**: `src/transport_common.cpp` (`openEarbudRfcomm`) + `SessionCapabilities` in `src/session.*` so both Linux (`qml/Main.qml`) and Windows (`qml/MainWin.qml`) UIs adapt automatically.
- **vivo / iQOO GAIA model overrides**: wire-level profile overrides can be tested without recompiling via `~/.config/kalm/models.json` (matching [`models.json`](models.json)).
