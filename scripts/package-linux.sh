#!/usr/bin/env bash
set -euo pipefail

# Creates an ordinary relocatable tar.gz. linuxdeploy is a transient build tool;
# neither an AppImage runtime nor AppRun is included in the distribution.
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir=""; output_dir=""; qt_root=""; tool_dir=""; version=""
while (($#)); do
    case "$1" in
        --build-dir) build_dir="${2:?}"; shift 2;;
        --output-dir) output_dir="${2:?}"; shift 2;;
        --qt-root) qt_root="${2:?}"; shift 2;;
        --tool-dir) tool_dir="${2:?}"; shift 2;;
        --version) version="${2:?}"; shift 2;;
        --help) printf '%s\n' 'package-linux.sh --build-dir DIR --output-dir DIR --qt-root DIR --tool-dir DIR --version X.Y.Z'; exit 0;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2;;
    esac
done
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'Linux x86_64 is required.' >&2; exit 2; }
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'A three-part version is required.' >&2; exit 2; }
[[ -n "$build_dir" && -n "$output_dir" && -n "$qt_root" && -n "$tool_dir" ]] || { echo 'All directory options are required.' >&2; exit 2; }
build_dir="$(realpath -- "$build_dir")"
qt_root="$(realpath -- "$qt_root")"
tool_dir="$(realpath -- "$tool_dir")"
mkdir -p -- "$output_dir"
output_dir="$(realpath -- "$output_dir")"
bundle_name="8bitCode-$version-linux-x86_64"
asset="$output_dir/$bundle_name.tar.gz"
[[ ! -e "$asset" ]] || { echo "Refusing to overwrite $asset" >&2; exit 2; }
grep -q '^BUILD_TESTING:BOOL=ON$' "$build_dir/CMakeCache.txt" || { echo 'Use the tested BUILD_TESTING=ON build.' >&2; exit 2; }
for requirement in cmake python3 timeout tar; do command -v "$requirement" >/dev/null; done
python3 "$repo_dir/scripts/release_support.py" tools --directory "$tool_dir" --tool linuxdeploy --tool linuxdeploy-plugin-qt --tool patchelf --verify-only
chmod +x "$tool_dir/linuxdeploy-x86_64.AppImage" "$tool_dir/linuxdeploy-plugin-qt-x86_64.AppImage"
work_dir="$(mktemp -d "$build_dir/linux-package.XXXXXX")"
app_dir="$work_dir/$bundle_name"
printf 'Packaging workspace (retained for diagnostics): %s\n' "$work_dir"
mkdir "$work_dir/patchelf"
tar -xzf "$tool_dir/patchelf-0.18.0-x86_64.tar.gz" -C "$work_dir/patchelf"
export BIT8PAD_PATCHELF_REAL="$work_dir/patchelf/bin/patchelf"
[[ -x "$BIT8PAD_PATCHELF_REAL" ]] || { echo 'Pinned patchelf executable missing.' >&2; exit 1; }
# The tar launcher supplies the library search path. Preserve original ELF
# layout: rewriting newer distributions' RELR sections can corrupt libraries.
# linuxdeploy still uses the real pinned tool for read-only ELF inspection.
export PATCHELF="$work_dir/patchelf-readonly"
cat > "$PATCHELF" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    case "$argument" in --set-rpath|--force-rpath|--remove-rpath|--shrink-rpath) exit 0;; esac
done
exec "$BIT8PAD_PATCHELF_REAL" "$@"
EOF
chmod 755 "$PATCHELF"
DESTDIR="$app_dir" cmake --install "$build_dir" --prefix /usr
[[ -x "$app_dir/usr/bin/8bitCode" ]] || { echo 'Installed executable is missing.' >&2; exit 1; }
# All paths below belong to the freshly created packaging directory.
rm -rf -- "$app_dir/usr/include" "$app_dir/usr/lib/cmake" "$app_dir/usr/lib64/cmake"
find "$app_dir/usr" -type f -name '*.a' -delete
export QMAKE="$qt_root/bin/qmake"
[[ -x "$QMAKE" ]] || export QMAKE="$qt_root/bin/qmake6"
[[ -x "$QMAKE" ]] || { echo 'Qt SDK query tool missing.' >&2; exit 1; }
qt_libraries="$("$QMAKE" -query QT_INSTALL_LIBS)"
qt_plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
export PATH="$tool_dir:$qt_root/bin:$PATH"
export LD_LIBRARY_PATH="$qt_libraries${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1 # Only the verified transient build tools use this.
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so
for platform in libqwayland-egl.so libqwayland-generic.so; do
    [[ ! -f "$qt_plugins/platforms/$platform" ]] || EXTRA_PLATFORM_PLUGINS+=";$platform"
done
# Distro libraries are already stripped; old bundled strip cannot handle RELR.
export NO_STRIP=1
"$tool_dir/linuxdeploy-x86_64.AppImage" --appdir "$app_dir" \
    --desktop-file "$app_dir/usr/share/applications/8bitcode.desktop" \
    --icon-file "$app_dir/usr/share/icons/hicolor/scalable/apps/8bitcode.svg" --plugin qt
for library in Core Core5Compat Gui Widgets Network PrintSupport Svg; do
    find "$app_dir/usr/lib" -name "libQt6$library.so*" -print -quit | grep -q . || { echo "Missing Qt $library runtime" >&2; exit 1; }
done
for plugin in libqxcb.so libqoffscreen.so libqsvg.so; do
    find "$app_dir" -name "$plugin" -print -quit | grep -q . || { echo "Missing $plugin" >&2; exit 1; }
done
# No AppImage launcher, desktop integration runtime or generated hook is shipped.
rm -f -- "$app_dir/AppRun" "$app_dir/AppRun.wrapped" "$app_dir/.DirIcon" "$app_dir/8bitcode.desktop" "$app_dir/8bitcode.svg"
rm -rf -- "$app_dir/apprun-hooks"
cat > "$app_dir/8bitCode" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
bundle_dir="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" && pwd)"
export LD_LIBRARY_PATH="$bundle_dir/usr/lib:$bundle_dir/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$bundle_dir/usr/plugins"
exec "$bundle_dir/usr/bin/8bitCode" "$@"
EOF
chmod 755 "$app_dir/8bitCode"
cat > "$app_dir/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix=..
Libraries=lib
Plugins=plugins
Translations=translations
EOF
qt_version="$("$QMAKE" -query QT_VERSION)"
cat > "$app_dir/usr/share/doc/8bitcode/QT-SOURCE.txt" <<EOF
Runtime Qt version: $qt_version (QtBase, Qt5Compat and QtSvg).
License notices and source metadata: licenses/qt/.
Official corresponding Qt sources:
https://download.qt.io/archive/qt/${qt_version%.*}/$qt_version/submodules/
CI release sources accompany the package as 8bitCode-$version-qt-6.5.3-source.tar.gz.
Application/dependency source: 8bitCode-$version-source.tar.gz.
EOF
# Normalize only documentation names; all runtime paths must already be portable.
python3 - "$app_dir" <<'PY'
from pathlib import Path
import re, sys
root = Path(sys.argv[1])
docs = root / 'usr/share/doc'
for path in sorted(docs.rglob('*'), key=lambda p: len(p.parts), reverse=True):
    safe = re.sub(r'[^A-Za-z0-9_.+@-]', '-', path.name)
    if safe != path.name:
        target = path.with_name(safe)
        if target.exists():
            raise SystemExit(f'Documentation name collision: {target}')
        path.rename(target)
for path in root.rglob('*'):
    if not re.fullmatch(r'[A-Za-z0-9_./+@-]+', str(path.relative_to(root))):
        raise SystemExit(f'Non-portable package filename: {path}')
    if path.is_symlink() and not path.resolve().is_relative_to(root.resolve()):
        raise SystemExit(f'External symlink in package: {path}')
PY
staged_archive="$work_dir/$bundle_name.tar.gz"
tar --dereference --hard-dereference --owner=0 --group=0 --numeric-owner \
    -czf "$staged_archive" -C "$work_dir" "$bundle_name"
mkdir "$work_dir/extracted"
tar -xzf "$staged_archive" -C "$work_dir/extracted" --no-same-owner
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_FONTDIR -u QML2_IMPORT_PATH -u APPIMAGE_EXTRACT_AND_RUN \
    PATH=/usr/bin:/bin QT_QPA_PLATFORM=offscreen timeout 180 \
    "$work_dir/extracted/$bundle_name/8bitCode" --smoke-test "$work_dir/package-smoke"
cp -- "$staged_archive" "$asset"
printf 'Linux archive packaged and smoke-tested: %s\n' "$asset"
