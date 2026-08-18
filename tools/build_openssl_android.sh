#!/usr/bin/env bash

set -euo pipefail

NDK="${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
API=29
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/.android-openssl}"

OPENSSL_VERSION="3.4.0"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"

BUILD_DIR="$(mktemp -d /tmp/openssl-android-build.XXXXXX)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "==> Downloading OpenSSL ${OPENSSL_VERSION}..."
curl -L "$OPENSSL_URL" | tar -xz -C "$BUILD_DIR"

cd "$BUILD_DIR/openssl-${OPENSSL_VERSION}"

export ANDROID_NDK_ROOT="$NDK"
export PATH="$TOOLCHAIN/bin:$PATH"

echo "==> Configuring OpenSSL for arm64-v8a (API ${API})..."
./Configure android-arm64 \
    -D__ANDROID_API__=${API} \
    no-shared \
    no-tests \
    no-docs \
    --prefix="$OUT_DIR" \
    --openssldir="$OUT_DIR"

echo "==> Building OpenSSL (this takes a few minutes)..."
make -j"$(nproc)"

echo "==> Installing to ${OUT_DIR}..."
make install_sw

echo ""
echo "Done. Set this for the Android cmake configure:"
echo "  cmake --preset android-arm64 -DANDROID_OPENSSL_PREFIX=${OUT_DIR}"
