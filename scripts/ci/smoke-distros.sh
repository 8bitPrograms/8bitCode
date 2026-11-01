#!/usr/bin/env bash
set -euo pipefail
app=$(python -c 'import json;print(json.load(open("packaging/release.json"))["app"])')
version=$(python scripts/ci/distribution.py version)
cpu="${RELEASE_ARCH:-$(uname -m)}"
case "$cpu" in x86_64)cpu=x64;;aarch64)cpu=arm64;;esac
bundle="$app-$version-linux-$cpu"
tmp=$(mktemp -d "$PWD/build/distro-smoke.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
tar -xzf "dist/release/$bundle.tar.gz" -C "$tmp"
images=(ubuntu:22.04 debian:12-slim fedora:44)
if [[ "$cpu" == x64 ]];then images+=(archlinux:base);fi
for image in "${images[@]}";do
 if [[ "$app" == 8bitCode ]];then probe=(--smoke-test /tmp/probe);else probe=(--screenshot /tmp/shell.png --window-size 1280x800);fi
 # A user with uid 1000 exists only for this disposable container. No host state changes.
 docker run --rm --user 1000:1000 --tmpfs /tmp:mode=1777 -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e QT_QUICK_BACKEND=software -e BIT8SHELL_PROFILE=/tmp/profile -e BIT8PAD_PROFILE=/tmp/profile -v "$tmp/$bundle:/bundle:ro" "$image" "/bundle/$app" "${probe[@]}"
done
