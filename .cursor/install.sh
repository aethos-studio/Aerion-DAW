#!/usr/bin/env bash
#
# Cloud Agent install script for Aerion DAW (Linux build environment).
#
# Aerion DAW targets Windows (MSVC) and macOS (Clang) as its primary/CI
# platforms. Cloud Agents run on Linux, so this script provisions a Linux
# build of the app + smoke tests using JUCE's built-in Linux support. It is
# intentionally environment-only: it does NOT modify any tracked source or the
# project's CMakeLists.txt. The two Linux-specific CMake tweaks below are passed
# as configure flags rather than committed to the build files:
#
#   * -DJUCE_WEB_BROWSER=0        The app never uses juce::WebBrowserComponent,
#                                 and the project's CMake (written for Win/mac)
#                                 does not wire JUCE's Linux WebKit/GTK deps into
#                                 its targets, so gui_extra fails on <gtk/gtk.h>.
#                                 Disabling the unused feature is the clean fix.
#   * -DCMAKE_CXX_STANDARD_LIBRARIES=-lcurl
#                                 juce_core defaults JUCE_USE_CURL=1 on Linux but
#                                 the project's CMake does not link libcurl on
#                                 Linux (curl is unused on Win/mac). Appending it
#                                 at the end of the link line resolves the JUCE
#                                 network symbols.
#
# The script is idempotent: re-running it re-uses the Tracktion Engine clone and
# the existing CMake build tree, so incremental runs are fast.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"
echo "==> Aerion DAW install (repo root: $REPO_ROOT)"

# ---------------------------------------------------------------------------
# 1. System build dependencies (toolchain + JUCE Linux libraries + Xvfb).
# ---------------------------------------------------------------------------
export DEBIAN_FRONTEND=noninteractive
echo "==> Installing system packages (apt)"
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
  build-essential g++ ninja-build cmake git pkg-config \
  libasound2-dev libjack-jackd2-dev \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libxrender-dev \
  libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev \
  xvfb x11-xserver-utils x11-utils xdotool

# ---------------------------------------------------------------------------
# 2. Pre-clone Tracktion Engine v3.2.0 with HTTPS submodules (idempotent).
#    Tracktion's .gitmodules references its JUCE submodule over SSH, which fails
#    on unauthenticated machines; rewrite it to HTTPS, exactly like CI does.
# ---------------------------------------------------------------------------
TE_DIR="$REPO_ROOT/tracktion_engine-src"
if [ -d "$TE_DIR/.git" ]; then
  echo "==> Tracktion Engine already cloned; ensuring submodules are present"
  git -C "$TE_DIR" submodule update --init --recursive --depth 1
else
  echo "==> Cloning Tracktion Engine v3.2.0"
  git clone --depth 1 --branch v3.2.0 \
    https://github.com/Tracktion/tracktion_engine.git "$TE_DIR"
  git -C "$TE_DIR" submodule set-url modules/juce https://github.com/juce-framework/JUCE.git
  git -C "$TE_DIR" submodule update --init --recursive --depth 1
fi

# ---------------------------------------------------------------------------
# 3. Configure + build the app and smoke tests (Ninja + GCC).
# ---------------------------------------------------------------------------
echo "==> Configuring CMake (Ninja, GCC, Debug + tests)"
CC=gcc CXX=g++ cmake -S "$REPO_ROOT/AerionDawCpp" -B "$REPO_ROOT/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAERION_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-DJUCE_WEB_BROWSER=0" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-lcurl" \
  -DFETCHCONTENT_SOURCE_DIR_TRACKTION_ENGINE="$TE_DIR"

echo "==> Building AerionDaw + AerionTests"
cmake --build "$REPO_ROOT/build" --target AerionDaw AerionTests --parallel "$(nproc)"

echo "==> Running smoke tests"
ctest --test-dir "$REPO_ROOT/build" -R "^AerionSmokeTests$" --output-on-failure

echo "==> Aerion DAW install complete."
echo "    App:   build/AerionDaw_artefacts/Debug/Aerion DAW"
echo "    Tests: build/AerionTests_artefacts/Debug/AerionTests"
echo "    Run the GUI with: DISPLAY=:99 \"build/AerionDaw_artefacts/Debug/Aerion DAW\""
