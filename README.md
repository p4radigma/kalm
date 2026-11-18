# Kalm

Native desktop application (**C++20 / Qt 6**) for controlling active noise cancellation (ANC), sound profiles, and device settings on **vivo / iQOO TWS**, **HUAWEI FreeBuds / FreeLace**, and **realme / OPPO / OnePlus** earbuds (verified on *vivo TWS 5 Hi-Fi*, *HUAWEI FreeBuds 6i*, and *realme Buds T300*) over Bluetooth RFCOMM on **Windows 10/11** and **Linux**.

> **Note:** This project was built with the assistance of AI and is intended primarily for personal use.

<p align="center">
  <img src="assets/screenshot.png" alt="Kalm Screenshot" width="520">
</p>

## Features
- **Noise Control & Official ANC Levels**:
  - *Noise Cancelling (ANC)* with all official intensity & environment presets: **Adaptive**, **Mild**, **Standard**, **Deep**, **Deep (Airplane)**, **Deep (Metro)**, **Deep (Bus / Transit)**.
  - *Transparency* with **Standard** and **Vocal enhancement** modes.
  - *Off*.
- **Multiple Earbuds**: add via Bluetooth scanner or manual MAC address, rename, remove, and switch manually or automatically when connected.
- **KDE Plasma System Tray**: mode-colored tray badge, middle-click mode cycling, right-click menu for modes, ANC levels, EQ, and saved earbuds.
- **Full Hardware Control**: Left/Right/Case battery & in-ear status, *DeepX / Ultimate Master (Hi-Fi)* EQ presets, low-latency Game Mode, stem long-press cycle, dual-device Multipoint management, and sound alert Finder.
- **Built-in Protocol Probe**: 1-click privacy-safe diagnostic dump (`kalm probe` / Drawer → *Diagnostic Report…*) for adding new earbud models.
- **Localization**: **English** (default) and **Russian** built-in, switchable live in **Drawer (☰) → Language** or via CLI, with zero-rebuild JSON extensibility.

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

**Linux — Option 2 (Lightweight Native Binary ~550 KB, uses system Qt6 & Kirigami):**
Install runtime packages for your distro (see [Dependencies](#dependencies)), download `kalm-linux-x86_64` from [Releases](https://github.com/p4radigma/kalm/releases), and run:
```bash
chmod +x kalm-linux-x86_64
./kalm-linux-x86_64
```

**After installing (`kalm` in `PATH` and Application Menu):**
```bash
kalm                                         # Launch GUI (or raise existing window)
kalm --minimized                             # Start minimized to system tray (ideal for autostart)
kalm status [--json]                         # Show battery, ANC mode/level, multipoint
kalm anc [adaptive|mild|standard|deep|plane|metro|bus] [--notify]
kalm trans [standard|voice] [--notify]
kalm off [--notify]
kalm toggle [--modes anc,trans] [--notify]   # Cycle modes (ideal for global shortcuts)
kalm eq master                               # standard | vocal | bass | treble | audiobook | master
kalm game on | off                           # Low-latency game mode
kalm lang en | ru | auto                     # Persist interface language
kalm probe [-d <MAC|name>] [--listen 15]     # Privacy-safe protocol dump for new earbuds
kalm devices [list|scan|add <MAC> --alias <name>|use <name>|rename <name> <new>|remove <name>]
```

---

## Supported Devices

| Status | Earbud Models |
|---|---|
| **Verified on Hardware** | **vivo TWS 5 Hi-Fi** (all ANC intensities & scenes, Vocal transparency, DeepX / Master Hi-Fi, Game Mode, Multipoint, Wear state, Battery) |
| **Supported (GAIA v4 Profile)** | **vivo TWS 5**, **vivo TWS 5 Pro**, **vivo TWS 5e**, **vivo TWS 4**, **vivo TWS 4 Hi-Fi**, **vivo TWS 3**, **vivo TWS 3 Pro**, **iQOO TWS 5**, **iQOO TWS 5e**, **iQOO TWS 2**, **iQOO TWS 1**, **iQOO TWS 1e**, **vivo / iQOO Headphones** |
| **Supported (GAIA v3 Profiles)** | **vivo TWS 3e**, **vivo TWS Air3 Pro**, **iQOO TWS Air3 Pro** |
| **Battery / EQ / Multipoint Only** | Non-ANC GAIA models (*vivo TWS 5i / 3i / Air3 / Air2*, *iQOO TWS 5i / 1i / Air3*) |
| **Verified on Hardware (HUAWEI SPP)** | **HUAWEI FreeBuds 6i** (ANC modes + levels including Dynamic, Transparency, battery per bud/case, EQ presets, Game Mode, auto-pause, dual-connect list) |
| **Supported (HUAWEI SPP)** | Other **HUAWEI FreeBuds / FreeLace / Eyewear** models exposing the SPP service on channel 1 (features depend on the model) |
| **Verified on Hardware (realme / OPPO Link)** | **realme Buds T300** (ANC / Transparency / Off, Left / Right / Case battery & charging, firmware, live push sync) |

*HUAWEI devices are recognized by their Bluetooth name and use the SPP profile on RFCOMM channel 1. The protocol implementation was ported from the GPL-3 [OpenFreebuds](https://github.com/melianmiko/OpenFreebuds) project.*

*Have another vivo / iQOO model? Run `kalm probe` and open a [New Earbud Model Support](#requesting-support-for-new-earbuds-what-users-need-to-do) issue.*

---

## Dependencies

| Scope | Packages / Components |
|---|---|
| **Build** | `cmake >= 3.20`, C++20 compiler (`g++ >= 11` or `clang++ >= 14`), `Qt6` headers (`Core`, `Gui`, `Widgets`, `DBus`, `Qml`, `Quick`) |
| **Runtime (Required)** | `BlueZ` + `D-Bus`, `Qt 6.5+` (`Base`, `Declarative/Quick`), `KDE Kirigami 6` (`org.kde.kirigami`), `Kirigami Addons` (`org.kde.kirigamiaddons.formcard`) |
| **Runtime (Recommended)** | `qqc2-desktop-style` & `breeze-icons` (native KDE styling), `libnotify` / `notify-send` (for `--notify`) |

---

## Installing Dependencies by Distro

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
  qml6-module-org-kde-desktop breeze-icon-theme bluez libnotify-bin
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
1. Copy `translations/en.json` to `~/.local/share/kalm/translations/de.json` and set `"lang_name"` (e.g. `"Deutsch"`).
2. Translate the values (untranslated keys automatically fall back to `en.json`).
3. Select it in **Drawer (☰) → Language** or run `kalm lang de`.

---

## Contributing

Contributions, bug fixes, translations, and new earbud profiles are welcome!

### AI Coding Agents Policy
- **AI coding agents and LLM-assisted PRs are explicitly allowed and welcome.**
- Please ensure every PR compiles cleanly (`cmake --build build`), passes all unit tests (`ctest --test-dir build --output-on-failure`), stays focused on a single concern, and clearly notes whether protocol changes were verified on real hardware or inferred.

### How to Open Issues
Use the repository Issue Templates:
- **Bug Report** — include your distro, desktop environment, Kalm version, earbud model, and output of `kalm status` / `kalm probe`.
- **New Earbud Model Support** — follow the steps below so we have everything needed to add your earbuds quickly.

### Requesting Support for New Earbuds (What Users Need to Do)
1. Pair and connect your earbuds via Bluetooth, and **take both earbuds out of the charging case**.
2. Collect a read-only diagnostic dump (personal MAC bytes and paired host names are automatically masked):
   - **From GUI:** open **Drawer (☰) → Diagnostic Report… → Copy Report to Clipboard**.
   - **From terminal:** run `kalm probe` (or `kalm probe -d "Your Earbuds Name"`).
3. *(Optional, very helpful if ANC modes differ)* Run live event capture for 15 seconds while toggling ANC / Transparency / Off by squeezing the earbud stem (or switching modes in the mobile app connected via multipoint):
   ```bash
   kalm probe --listen 15
   ```
4. Open a **New Earbud Model Support** issue and paste the output along with the list of features available in the official mobile app.

### Adding New Earbud Profiles (Maintainer / Contributor Guide)
Wire-level differences between vivo / iQOO generations (GAIA v3 vs v4, noise query byte, and noise payload suffix) are defined in [`models.json`](models.json). You can test a new model profile locally **without recompiling** by creating `~/.config/kalm/models.json` with the same structure, then submit the verified entry in a PR.
