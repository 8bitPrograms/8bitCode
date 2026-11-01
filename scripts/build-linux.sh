#!/usr/bin/env bash
set -euo pipefail

# Requires a C++20 compiler, CMake >=3.21, Ninja, and a Qt >=6.5 desktop SDK
# with Core5Compat, LinguistTools, Concurrent, Test and SVG support.
# Set CMAKE_PREFIX_PATH to the Qt SDK root when Qt is not system-installed.
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BIT8PAD_BUILD_DIR:-$repo_dir/build-linux}"

if command -v qmake6 >/dev/null 2>&1; then
    export QMAKE="${QMAKE:-$(command -v qmake6)}"
elif command -v qmake >/dev/null 2>&1; then
    export QMAKE="${QMAKE:-$(command -v qmake)}"
fi

cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "$@"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
    ctest --test-dir "$build_dir" --output-on-failure --no-tests=error --timeout 90

printf 'Build and tests completed. Executable: %s/src/8bitCode\n' "$build_dir"
