#!/usr/bin/env bash
# PQCH: build the light wallet core (libpqchwallet.so) for Android and put it in the app.
#
#   ANDROID_NDK_HOME=/path/to/ndk contrib/pqch/android/build-core.sh [abi ...]
#
# ABIs: arm64-v8a (phones, default) and x86_64 (emulator). Only Boost headers are needed, taken
# from depends (downloaded and checked against its SHA256 there).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
APP="${ROOT}/contrib/pqch/android"
NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to the Android NDK}"
ABIS=("$@")
[ ${#ABIS[@]} -gt 0 ] || ABIS=(arm64-v8a)
API=26   # Android 8.0, the app's minSdk
JOBS="$(nproc 2>/dev/null || echo 4)"
WORK="${ROOT}/build-android"

echo "== Boost headers (depends)"
make -C "${ROOT}/depends" boost NO_QT=1 -j"${JOBS}" >/dev/null
TARBALL="$(ls "${ROOT}"/depends/built/*/boost/boost-*.tar.gz | head -1)"
rm -rf "${WORK}/boost" && mkdir -p "${WORK}/boost"
tar xzf "${TARBALL}" -C "${WORK}/boost"
BOOST_INC="${WORK}/boost/boost/include"
test -f "${BOOST_INC}/boost/version.hpp"

for ABI in "${ABIS[@]}"; do
    echo "== libpqchwallet for ${ABI}"
    B="${WORK}/${ABI}"
    cmake -S "${ROOT}" -B "${B}" -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="${NDK}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="${ABI}" -DANDROID_PLATFORM="android-${API}" -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DPQCH_BOOST_INCLUDE_DIR="${BOOST_INC}" \
        -DBUILD_PQCH_LIGHTWALLET=ON \
        -DBUILD_BITCOIN_BIN=OFF -DBUILD_DAEMON=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF -DBUILD_TX=OFF \
        -DBUILD_UTIL=OFF -DBUILD_GUI=OFF -DENABLE_WALLET=OFF -DENABLE_IPC=OFF \
        -DENABLE_EXTERNAL_SIGNER=OFF -DWITH_EMBEDDED_ASMAP=OFF -DWITH_CCACHE=OFF
    cmake --build "${B}" --target pqchwallet -j"${JOBS}"
    OUT="${APP}/app/src/main/jniLibs/${ABI}"
    mkdir -p "${OUT}"
    "${NDK}"/toolchains/llvm/prebuilt/*/bin/llvm-strip --strip-unneeded -o "${OUT}/libpqchwallet.so" "${B}/lib/libpqchwallet.so"
    ls -l "${OUT}/libpqchwallet.so"
done
