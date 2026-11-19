# Building & Dependencies

[← Back to README](../README.md)

---

## Linux Dependencies

> **Note:** If you use **`Kalm-x86_64.AppImage`**, Qt 6 and KDE Kirigami are already bundled inside the AppImage — only `BlueZ` (Bluetooth) is required on the host system. The packages below are needed only when building from source or running the standalone `kalm-linux-x86_64` binary.

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

## Building on Linux

```bash
git clone https://github.com/p4radigma/kalm.git
cd kalm
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Installing on Linux

System-wide (`/usr/local`):
```bash
sudo cmake --install build --strip
```

User-local (`~/.local`, no root required):
```bash
cmake --install build --prefix ~/.local --strip
```

Packaging a self-contained AppImage:
```bash
./scripts/build-appimage.sh build Kalm-x86_64.AppImage
```

---

## Building on Windows (x64)

### Prerequisites
- **Visual Studio 2022** (Desktop development with C++ + Windows 10/11 SDK)
- **CMake >= 3.20** and **Ninja**
- **Qt 6.8+** (`msvc2022_64`, including `QtQuick` / `QtQuickControls2`)
- *(Optional, for building the installer)* **[Inno Setup 6](https://jrsoftware.org/isinfo.php)**

### Build, Test & Deploy (Developer PowerShell for VS 2022)
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V

New-Item -ItemType Directory -Force -Path dist/Kalm-win64 | Out-Null
Copy-Item build/kalm.exe dist/Kalm-win64/kalm.exe
Copy-Item models.json, README.md, LICENSE dist/Kalm-win64/
Copy-Item -Recurse translations dist/Kalm-win64/translations
windeployqt --release --qmldir qml --no-translations --no-opengl-sw --compiler-runtime dist/Kalm-win64/kalm.exe
```

To compile `dist/Kalm-win64-setup.exe` with Inno Setup 6:
```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "packaging\windows\kalm.iss"
```
