#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
OUT_FILE="${2:-$ROOT_DIR/Kalm-x86_64.AppImage}"

if [[ ! -x "$BUILD_DIR/kalm" ]]; then
    echo "Error: '$BUILD_DIR/kalm' not found. Build Kalm first." >&2
    exit 1
fi

QMAKE_BIN=""
for cand in qmake6 /usr/lib64/qt6/bin/qmake /usr/lib/qt6/bin/qmake /usr/lib/x86_64-linux-gnu/qt6/bin/qmake; do
    if command -v "$cand" >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v "$cand")"
        break
    fi
done
if [[ -z "$QMAKE_BIN" ]]; then
    echo "Error: qmake6 not found." >&2
    exit 1
fi

QT_PLUGINS="$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS)"
QT_QML="$("$QMAKE_BIN" -query QT_INSTALL_QML)"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/kalm-appdir.XXXXXX")"
APPDIR="$WORK_DIR/AppDir"
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p \
    "$APPDIR/usr/bin" \
    "$APPDIR/usr/lib" \
    "$APPDIR/usr/plugins" \
    "$APPDIR/usr/qml" \
    "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
    "$APPDIR/usr/share/kalm"

# 1. Binary, desktop file, icon, translations, models
cp "$BUILD_DIR/kalm" "$APPDIR/usr/bin/kalm"
strip -s "$APPDIR/usr/bin/kalm" || true

sed \
    -e 's|@CMAKE_INSTALL_FULL_BINDIR@/kalm|kalm|g' \
    -e 's|^Icon=.*|Icon=io.github.kalm|g' \
    "$ROOT_DIR/io.github.kalm.desktop.in" > "$APPDIR/io.github.kalm.desktop"
cp "$APPDIR/io.github.kalm.desktop" "$APPDIR/usr/share/applications/io.github.kalm.desktop"

cp "$ROOT_DIR/assets/io.github.kalm.png" "$APPDIR/io.github.kalm.png"
cp "$ROOT_DIR/assets/io.github.kalm.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/io.github.kalm.png"
cp -r "$ROOT_DIR/translations" "$APPDIR/usr/share/kalm/"
cp "$ROOT_DIR/models.json" "$APPDIR/usr/share/kalm/models.json"

# 2. Required Qt6 & KF6 plugins (excluding heavy kimg_* RAW/AVIF/HEIF plugins)
copy_plugin_dir() {
    local rel="$1"
    if [[ -d "$QT_PLUGINS/$rel" ]]; then
        mkdir -p "$APPDIR/usr/plugins/$(dirname "$rel")"
        cp -a "$QT_PLUGINS/$rel" "$APPDIR/usr/plugins/$rel"
    fi
}

for pdir in \
    platforms \
    wayland-graphics-integration-client \
    wayland-shell-integration \
    wayland-decoration-client \
    xcbglintegrations \
    iconengines \
    styles \
    kf6/kirigami \
    kf6/kwindowsystem; do
    copy_plugin_dir "$pdir"
done

mkdir -p "$APPDIR/usr/plugins/imageformats"
for img_so in libqsvg.so libqico.so libqjpeg.so libqwebp.so libqgif.so; do
    if [[ -f "$QT_PLUGINS/imageformats/$img_so" ]]; then
        cp -a "$QT_PLUGINS/imageformats/$img_so" "$APPDIR/usr/plugins/imageformats/"
    fi
done

mkdir -p "$APPDIR/usr/plugins/platformthemes"
if [[ -f "$QT_PLUGINS/platformthemes/libqxdgdesktopportal.so" ]]; then
    cp -a "$QT_PLUGINS/platformthemes/libqxdgdesktopportal.so" "$APPDIR/usr/plugins/platformthemes/"
fi

# 3. Required QML modules (QtQuick + KF6 Kirigami + FormCard + org.kde.desktop)
copy_qml_dir() {
    local rel="$1"
    if [[ -d "$QT_QML/$rel" ]]; then
        mkdir -p "$APPDIR/usr/qml/$(dirname "$rel")"
        cp -a "$QT_QML/$rel" "$APPDIR/usr/qml/$rel"
    fi
}

for qdir in \
    QtCore \
    QtQml \
    Qt/labs/platform \
    Qt/labs/qmlmodels \
    Qt/labs/settings \
    org/kde/kirigami \
    org/kde/kirigamiaddons/components \
    org/kde/kirigamiaddons/delegates \
    org/kde/kirigamiaddons/formcard \
    org/kde/kirigamiaddons/dateandtime \
    org/kde/desktop \
    org/kde/qqc2desktopstyle \
    org/kde/sonnet \
    org/kde/coreaddons \
    org/kde/iconthemes \
    org/kde/kitemmodels; do
    copy_qml_dir "$qdir"
done

# Copy core QtQuick sub-modules without QtQuick3D / Timeline / Pdf bloat
mkdir -p "$APPDIR/usr/qml/QtQuick"
for f in "$QT_QML/QtQuick/"*; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f")"
    if [[ -f "$f" ]]; then
        cp -a "$f" "$APPDIR/usr/qml/QtQuick/"
    else
        case "$base" in
            Controls|Dialogs|Effects|Layouts|NativeStyle|Shapes|Templates|VectorImage|Window|PrivateWidgets)
                cp -a "$f" "$APPDIR/usr/qml/QtQuick/"
                ;;
        esac
    fi
done

# 4. Minimal Breeze icon set fallback (so icons work on GNOME / Hyprland / Sway without breeze-icons installed)
if [[ -d /usr/share/icons/breeze ]]; then
    mkdir -p "$APPDIR/usr/share/icons/breeze"
    [[ -f /usr/share/icons/breeze/index.theme ]] && cp -a /usr/share/icons/breeze/index.theme "$APPDIR/usr/share/icons/breeze/"
    python3 - "$APPDIR/usr/share/icons/breeze" << 'PYEOF'
import os, shutil, sys
dst_root = sys.argv[1]
src_root = "/usr/share/icons/breeze"
prefixes = (
    "audio-headset", "audio-headphones", "microphone-sensitivity",
    "list-add", "view-refresh", "network-bluetooth", "network-connect",
    "network-disconnect", "preferences-system-bluetooth", "preferences-desktop-notification",
    "window-minimize", "window-close", "application-exit", "edit-rename",
    "edit-delete", "edit-copy", "dialog-ok", "dialog-warning", "dialog-close",
    "dialog-cancel", "emblem-checked", "media-playback-stop", "tools-report-bug",
    "application-menu", "overflow-menu", "go-next", "go-previous", "go-up", "go-down",
)
for cur, _, files in os.walk(src_root):
    for fn in files:
        if fn.startswith(prefixes):
            src_file = os.path.join(cur, fn)
            rel = os.path.relpath(src_file, src_root)
            dst_file = os.path.join(dst_root, rel)
            os.makedirs(os.path.dirname(dst_file), exist_ok=True)
            if os.path.islink(src_file):
                target = os.readlink(src_file)
                if not os.path.lexists(dst_file):
                    os.symlink(target, dst_file)
                    # Ensure target file in same dir is copied too
                    t_src = os.path.normpath(os.path.join(os.path.dirname(src_file), target))
                    if os.path.isfile(t_src):
                        t_rel = os.path.relpath(t_src, src_root)
                        t_dst = os.path.join(dst_root, t_rel)
                        os.makedirs(os.path.dirname(t_dst), exist_ok=True)
                        if not os.path.exists(t_dst):
                            shutil.copy2(t_src, t_dst)
            else:
                if not os.path.exists(dst_file):
                    shutil.copy2(src_file, dst_file)
PYEOF
fi

# 5. Recursively resolve & bundle shared library dependencies (excluding host ABI/GPU/Wayland/X11/DBus libs)
python3 - "$APPDIR" << 'PYEOF'
import os, re, shutil, subprocess, sys

appdir = sys.argv[1]
lib_dir = os.path.join(appdir, "usr", "lib")

# NOTE: libselinux is deliberately not excluded below. libgio (pulled in
# through the Qt/KF6 stack) links against it, and distributions such as Arch
# Linux do not ship libselinux at all, so the AppImage has to bundle it.
EXCLUDE_PREFIXES = (
    "linux-vdso.so", "ld-linux", "libc.so", "libm.so", "libdl.so", "librt.so",
    "libpthread.so", "libresolv.so", "libutil.so", "libnsl.so", "libcrypt.so",
    "libgcc_s", "libstdc++.so",
    "libGL.so", "libGLX.so", "libGLdispatch.so", "libEGL.so", "libOpenGL.so",
    "libgbm.so", "libdrm.so", "libdrm_",
    "libwayland-client.so", "libwayland-cursor.so", "libwayland-egl.so", "libwayland-server.so",
    "libX11.so", "libX11-xcb.so", "libxcb.so", "libXau.so", "libXdmcp.so",
    "libXext.so", "libXfixes.so", "libXrender.so", "libXcursor.so",
    "libdbus-1.so", "libfontconfig.so", "libfreetype.so", "libharfbuzz.so",
    "libz.so", "libexpat.so", "libsystemd.so", "libudev.so",
    "libmount.so", "libblkid.so",
)

def is_excluded(soname: str) -> bool:
    return soname.startswith(EXCLUDE_PREFIXES)

queue = []
for sub in ("bin", "plugins", "qml"):
    base = os.path.join(appdir, "usr", sub)
    for root, _, files in os.walk(base):
        for fn in files:
            p = os.path.join(root, fn)
            if not os.path.islink(p) and (fn == "kalm" or ".so" in fn):
                queue.append(p)

seen_bins = set()
bundled_sonames = set()

while queue:
    target = queue.pop()
    real_t = os.path.realpath(target)
    if real_t in seen_bins:
        continue
    seen_bins.add(real_t)

    try:
        out = subprocess.check_output(["ldd", target], text=True, stderr=subprocess.DEVNULL)
    except Exception:
        continue

    for line in out.splitlines():
        m = re.search(r"^\s*([^\s]+)\s*=>\s*([^\s]+)\s*\(", line)
        if not m:
            continue
        soname, resolved = m.group(1), m.group(2)
        if not os.path.isfile(resolved) or is_excluded(soname):
            continue
        if soname in bundled_sonames:
            continue
        bundled_sonames.add(soname)
        dst = os.path.join(lib_dir, soname)
        shutil.copy2(os.path.realpath(resolved), dst)
        queue.append(dst)

print(f"Bundled {len(bundled_sonames)} shared libraries into AppDir/usr/lib")
PYEOF

# Strip bundled shared libraries to minimize AppImage size
find "$APPDIR/usr/lib" "$APPDIR/usr/plugins" "$APPDIR/usr/qml" -type f -name "*.so*" -exec strip --strip-unneeded {} + 2>/dev/null || true

# 6. Write self-contained AppRun
cat << 'EOF' > "$APPDIR/AppRun"
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML2_IMPORT_PATH="$HERE/usr/qml"
export QML_IMPORT_PATH="$HERE/usr/qml"
export QT_QUICK_CONTROLS_STYLE="${QT_QUICK_CONTROLS_STYLE:-org.kde.desktop}"
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-xdgdesktopportal}"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$HERE/usr/bin/kalm" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# 7. Package into AppImage using official appimagetool (works without FUSE via APPIMAGE_EXTRACT_AND_RUN=1)
APPIMAGETOOL="${APPIMAGETOOL:-}"
if [[ -z "$APPIMAGETOOL" ]]; then
    if command -v appimagetool >/dev/null 2>&1; then
        APPIMAGETOOL="$(command -v appimagetool)"
    else
        APPIMAGETOOL="$WORK_DIR/appimagetool-x86_64.AppImage"
        curl -fsSL "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" -o "$APPIMAGETOOL"
        chmod +x "$APPIMAGETOOL"
    fi
fi

ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APPDIR" "$OUT_FILE"
chmod +x "$OUT_FILE"
ls -lh "$OUT_FILE"
