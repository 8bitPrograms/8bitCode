#!/bin/sh
# The installer uses only tools supplied by a supported desktop OS.
set -eu
umask 022
app='8bitCode'
command_names='8bitcode 8bitpad 8p'
slug='8bitcode'
repository="${BIT8PAD_REPOSITORY:-@REPOSITORY@}"
version=latest
system=''
cpu=''
experimental=0
download_only=0
portable=''
fixture=''
install_root="${HOME}/.local/opt/8bitcode"
bin_dir="${HOME}/.local/bin"
data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}"
apps_dir="${HOME}/Applications"
download_dir=$PWD
explicit_paths=0
die(){ printf '%s installation stopped: %s\n' "$app" "$*" >&2; exit 1; }
need(){ [ "$#" -ge 2 ] && [ -n "$2" ] || die "Missing value for $1"; }
while [ "$#" -gt 0 ]; do
 case "$1" in
  --repo|--repository) need "$@";repository=$2;shift 2;;
  --version) need "$@";version=$2;shift 2;;
  --download-only) download_only=1;shift;;
  --download-dir) need "$@";download_dir=$2;shift 2;;
  --portable) need "$@";portable=$2;shift 2;;
  --install-dir) need "$@";install_root=$2;explicit_paths=$((explicit_paths+1));shift 2;;
  --bin-dir) need "$@";bin_dir=$2;explicit_paths=$((explicit_paths+1));shift 2;;
  --data-dir) need "$@";data_dir=$2;explicit_paths=$((explicit_paths+1));shift 2;;
  --applications-dir) need "$@";apps_dir=$2;shift 2;;
  --experimental) experimental=1;shift;;
  --arch) need "$@";cpu=$2;shift 2;;
  --fixture-directory) need "$@";fixture=$2;shift 2;;
  --fixture-os) need "$@";system=$2;shift 2;;
  --help|-h) printf '%s\n' "Usage: install.sh --repo OWNER/REPO [--version 1.0.0] [--portable DIRECTORY] [--download-only] [--experimental]" "Linux and macOS: OS/CPU detection, verified release bundle, per-user install; no Python, jq, Qt SDK, Homebrew, or sudo.";exit 0;;
  *) die "Unknown option: $1";;
 esac
done
case "$repository" in ''|*@*|OWNER/*) die 'Set --repo to the actual GitHub owner/repository.';;esac
printf '%s\n' "$repository" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' || die 'Invalid repository.'
if [ -z "$system" ]; then
 case "$(uname -s)" in Linux)system=linux;;Darwin)system=macos;;*)die 'Use install.ps1 on Windows.';;esac
else [ -n "$fixture" ] || die '--fixture-os requires fixture mode.'
fi
native_cpu=$(uname -m)
if [ -z "$cpu" ]; then
 case "$native_cpu" in x86_64|amd64)cpu=x64;;aarch64|arm64)cpu=arm64;;i386|i686)cpu=x86;;armv7l|armv7*)cpu=armv7;;*)die "Unsupported CPU: $native_cpu";;esac
elif [ "$download_only" = 0 ] && [ -z "$fixture" ]; then die '--arch overrides are restricted to download-only mode.'
fi
case "$cpu" in x64|arm64|x86|armv7);;*)die 'Unsupported architecture.';;esac
case "$system" in linux|macos);;*)die 'Unsupported operating system.';;esac
if [ -n "$fixture" ] && [ "$download_only" = 0 ] && [ "$explicit_paths" -ne 3 ]; then die 'Fixture installs require explicit install, bin, and data directories.';fi
if [ "$version" != latest ]; then
 version="${version#v}"
 printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die 'Invalid version.'
fi
for path in "$install_root" "$bin_dir" "$data_dir" "$apps_dir" "$download_dir" "$portable"; do
 case "$path" in *'"'*|*'$'*|*'`'*|*'
'*|*'
'*) die 'Installation paths must not contain quotes, shell expansions, or line breaks.';;esac
done
command -v curl >/dev/null 2>&1 || [ -n "$fixture" ] || die 'The curl command is unavailable.'
work=$(mktemp -d "${TMPDIR:-/tmp}/8bitCode.install.XXXXXX")
cleanup(){ case "$work" in */8bitCode.install.*)rm -rf -- "$work";;esac; }
trap cleanup EXIT HUP INT TERM
fetch(){
 name=$1;dest=$2;release=$3
 if [ -n "$fixture" ]; then cp -- "$fixture/$name" "$dest"
 else
  if [ "$release" = latest ];then url="https://github.com/$repository/releases/latest/download/$name"
  else url="https://github.com/$repository/releases/download/v$release/$name";fi
  curl --fail --location --silent --show-error --proto '=https' --tlsv1.2 "$url" -o "$dest"
 fi
}
fetch install-index.tsv "$work/index" "$version"
row=$(awk -F '\t' -v os="$system" -v arch="$cpu" '$2==os && $3==arch && $4=="portable"{print}' "$work/index")
[ "$(printf '%s\n' "$row" | awk 'NF{n++}END{print n+0}')" = 1 ] || die "No unique $system/$cpu package is available in this release."
release_version=$(printf '%s\n' "$row" | cut -f1)
asset=$(printf '%s\n' "$row" | cut -f5)
expected=$(printf '%s\n' "$row" | cut -f6)
glibc=$(printf '%s\n' "$row" | cut -f7)
status=$(printf '%s\n' "$row" | cut -f8)
printf '%s\n' "$release_version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die 'Invalid version in release index.'
[ "$version" = latest ] || [ "$version" = "$release_version" ] || die 'Release index version mismatch.'
[ "$asset" = "$app-$release_version-$system-$cpu.tar.gz" ] || [ "$asset" = "$app-$release_version-$system-$cpu.zip" ] || die 'Unexpected asset name.'
printf '%s\n' "$expected" | grep -Eq '^[0-9a-f]{64}$' || die 'Invalid release SHA256.'
[ "$status" = stable ] || [ "$experimental" = 1 ] || die 'This architecture is experimental; use --experimental after checking its release notes.'
if [ "$system" = linux ] && [ "$download_only" = 0 ] && [ -z "$fixture" ]; then
 actual=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}') || die 'This release requires glibc Linux.'
 awk -v have="$actual" -v need="$glibc" 'BEGIN{split(have,h,".");split(need,n,".");exit !(h[1]>n[1]||(h[1]==n[1]&&h[2]>=n[2]))}' || die "This release requires glibc $glibc or newer."
fi
fetch "$asset" "$work/$asset" "$release_version"
if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$work/$asset" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then actual=$(shasum -a 256 "$work/$asset" | awk '{print $1}')
else die 'The OS SHA256 tool is unavailable.';fi
[ "$actual" = "$expected" ] || die 'Package checksum mismatch; nothing was installed.'
if [ "$download_only" = 1 ]; then
 mkdir -p -- "$download_dir";[ ! -e "$download_dir/$asset" ] || die 'Download destination already exists.'
 mv -- "$work/$asset" "$download_dir/$asset";printf '%s\n' "$download_dir/$asset";exit 0
fi
mkdir "$work/unpacked"
base="$app-$release_version-$system-$cpu"
if [ "$system" = linux ];then
 tar -tzf "$work/$asset" > "$work/names"
 while IFS= read -r n;do
  case "$n" in "$base"|"$base/"|"$base/"*) ;;*)die 'Unexpected archive root.';;esac
  case "$n" in /*|../*|*/../*|*/..|*\\*)die 'Unsafe archive path.';;esac
 done < "$work/names"
 tar -tvzf "$work/$asset" | awk 'substr($0,1,1)=="l"||substr($0,1,1)=="h"{bad=1}END{exit bad}' || die 'Linux bundles must not contain links.'
 tar -xzf "$work/$asset" -C "$work/unpacked" --no-same-owner
else
 unzip -Z1 "$work/$asset" > "$work/names"
 while IFS= read -r n;do
  case "$n" in "$base/"*|__MACOSX/*);;*)die 'Unexpected ZIP root.';;esac
  case "$n" in /*|../*|*/../*|*/..|*\\*)die 'Unsafe ZIP path.';;esac
 done < "$work/names"
 ditto -xk "$work/$asset" "$work/unpacked"
fi
payload="$work/unpacked/$base";[ -d "$payload" ] || die 'Release bundle is missing.'
if [ -n "$portable" ];then
 [ ! -e "$portable" ] || die 'Portable destination already exists.'
 mkdir -p -- "$(dirname -- "$portable")";mv -- "$payload" "$portable"
 printf '%s\n' "Portable $app is ready in $portable. Use Start-Portable.sh (Linux) or Start-Portable.command (macOS).";exit 0
fi
mkdir -p -- "$install_root/versions"
dest="$install_root/versions/$release_version-$cpu"
if [ -e "$dest" ];then
 [ -f "$dest/.release-sha256" ] && [ "$(cat "$dest/.release-sha256")" = "$expected" ] || die 'Version destination already contains different files.'
else
 printf '%s\n' "$expected" > "$payload/.release-sha256"
 mv -- "$payload" "$dest"
fi
current="$install_root/current";next="$install_root/.current-$$"
[ ! -e "$current" ] || [ -L "$current" ] || die 'Current install path is not a managed link.'
ln -s "$dest" "$next"
if [ "$system" = linux ];then mv -Tf -- "$next" "$current";else mv -fh -- "$next" "$current";fi
if [ "$system" = linux ];then
 mkdir -p -- "$bin_dir" "$data_dir/applications"
 printf '#!/bin/sh\nexec "%s/current/%s" "$@"\n' "$install_root" "$app" > "$bin_dir/$app";chmod 755 "$bin_dir/$app"
 printf '[Desktop Entry]\nType=Application\nName=%s\nExec="%s/%s" %%F\nIcon=%s/current/share/icons/hicolor/scalable/apps/%s.svg\nTerminal=false\nCategories=Development;Utility;\n' "$app" "$bin_dir" "$app" "$install_root" "$slug" > "$data_dir/applications/$slug.desktop"
else
 mkdir -p -- "$apps_dir";target="$apps_dir/$app.app"
 [ ! -e "$target" ] || [ -L "$target" ] || die 'An unmanaged application already exists in Applications.'
 ln -s "$current/$app.app" "$apps_dir/.$app.next-$$";mv -fh -- "$apps_dir/.$app.next-$$" "$target"
fi
mkdir -p -- "$bin_dir"
if [ "$system" = linux ];then
 command_target="$install_root/current/$app"
else
 command_target="$install_root/current/$app.app/Contents/MacOS/$app"
fi
# 8bitpad and 8p were written by the 8bitPad installer; take over only those exact launchers.
legacy_command(){
 [ ! -L "$1" ] && [ "$(sed -n 1p "$1")" = '#!/bin/sh' ] && awk 'END{exit NR!=2}' "$1" || return 1
 case "$(sed -n 2p "$1")" in 'exec "'*'/current/8bitPad" "$@"'|'exec "'*'/current/8bitPad.app/Contents/MacOS/8bitPad" "$@"')return 0;;esac
 return 1
}
for command_name in $command_names;do
 command_file="$bin_dir/$command_name"
 # An alias may already belong to another tool. Never overwrite it.
 if { [ -e "$command_file" ] || [ -L "$command_file" ]; } && ! legacy_command "$command_file";then
  [ ! -L "$command_file" ] && printf '#!/bin/sh\nexec "%s" "$@"\n' "$command_target" | cmp -s - "$command_file" || die "Command already belongs to another installation: $command_file"
 else
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$command_target" > "$command_file"
  chmod 755 "$command_file"
 fi
done
case ":$PATH:" in *":$bin_dir:"*) :;; *) printf 'Commands installed in %s; add that directory to your shell PATH.\n' "$bin_dir";; esac
printf '%s\n' "$app $release_version installed for your user. Existing profiles and previous versions were kept."
