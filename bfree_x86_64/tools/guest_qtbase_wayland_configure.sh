#!/usr/bin/env bash
# Shared cmake configure line for guest qtbase + Wayland (used by fix/recover/impl).
# shellcheck shell=bash
guest_qtbase_wayland_configure() {
  local bd="$1" host_qt="$2" qt_src="$3" wayland_prefix="$4"
  cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$bd/toolchain.cmake" \
    -DQT_HOST_PATH="$host_qt" \
    -DCMAKE_INSTALL_PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_qtwebengine=OFF \
    -DQT_BUILD_EXAMPLES=FALSE \
    -DQT_BUILD_TESTS=FALSE \
    -DINPUT_opengl=no \
    -DFEATURE_vulkan=OFF \
    -DFEATURE_dbus=OFF \
    -DINPUT_openssl=no \
    -DFEATURE_network=OFF \
    -DFEATURE_ssl=OFF \
    -DFEATURE_brotli=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_glib=OFF \
    -DFEATURE_xcb=OFF \
    -DFEATURE_xkbcommon=OFF \
    -DFEATURE_xcb_xlib=OFF \
    -DFEATURE_system_zlib=OFF \
    -DINPUT_libpng=qt \
    -DINPUT_freetype=qt \
    -DINPUT_harfbuzz=qt \
    -DINPUT_pcre=qt \
    -DFEATURE_fontconfig=OFF \
    -DFEATURE_libudev=OFF \
    -DFEATURE_libinput=OFF \
    -DFEATURE_evdev=OFF \
    -DQT_FEATURE_libudev=OFF \
    -DQT_FEATURE_libinput=OFF \
    -DQT_FEATURE_evdev=OFF \
    -DWayland_DIR="$wayland_prefix/lib/cmake/Wayland" \
    -B "$bd" \
    -S "$qt_src/qtbase"
}
