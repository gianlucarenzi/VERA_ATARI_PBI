#!/usr/bin/env bash
# Build FujiNet-PC for ATARI from source and install the artifacts.
#
# Usage:
#   ./build-fujinet-pc.sh <SRC_DIR> <INSTALL_DIR>
#
# SRC_DIR     - source directory (the one containing CMakeLists.txt)
# INSTALL_DIR - target directory (the one with fujinet, run-fujinet, data/, SD/)
#
# User files (fnconfig.ini, SD/) are never touched.

set -euo pipefail

# ── arguments ──────────────────────────────────────────────────────────────

if [ $# -ne 2 ]; then
    echo "Usage: $0 <SRC_DIR> <INSTALL_DIR>" >&2
    exit 1
fi

SRC_DIR="$(realpath "$1")"
INSTALL_DIR="$(realpath "$2")"

if [ ! -f "$SRC_DIR/CMakeLists.txt" ]; then
    echo "Error: $SRC_DIR does not contain CMakeLists.txt" >&2
    exit 1
fi

if [ ! -d "$INSTALL_DIR" ]; then
    echo "Error: $INSTALL_DIR does not exist" >&2
    exit 1
fi

# ── build parameters ───────────────────────────────────────────────────────

NCPU=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 1)
# build.sh uses $SRC_DIR/build as build directory
BUILD_DIR="$SRC_DIR/build"
DIST_DIR="$BUILD_DIR/dist"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "=== FujiNet-PC build ==="
echo "  Source     : $SRC_DIR"
echo "  Build dir  : $BUILD_DIR"
echo "  Install dir: $INSTALL_DIR"
echo "  Build type : $BUILD_TYPE"
echo "  CPU cores  : $NCPU"
echo ""

# ── safeguard version.h ─────────────────────────────────────────────────────
# FujiNet-PC build requires include/version.h to exist.
# If it's missing (e.g. in some nightly checkouts), create a default one.

if [ ! -f "$SRC_DIR/include/version.h" ]; then
    echo ">>> Creating default include/version.h ..."
    mkdir -p "$SRC_DIR/include"
    cat > "$SRC_DIR/include/version.h" <<EOF
#define FN_VERSION_MAJOR 1
#define FN_VERSION_MINOR 6
#define FN_VERSION_PATCH 1
#define FN_VERSION_FULL "1.6.1-dev"
#define FN_VERSION_DATE "$(date +'%Y-%m-%d %H:%M:%S')"
EOF
fi

# ── build with build.sh ─────────────────────────────────────────────────────
# Align with .github/workflows/build-fujinet-pc.yml: use build.sh -p ATARI

echo ">>> building with ./build.sh -p ATARI ..."
cd "$SRC_DIR"

# Ensure we are building for Release unless specified
BUILD_FLAGS=""
if [ "$BUILD_TYPE" = "Debug" ]; then
    BUILD_FLAGS="-g"
fi

./build.sh -p ATARI $BUILD_FLAGS

# ── verify artifacts ───────────────────────────────────────────────────────

if [ ! -f "$DIST_DIR/fujinet" ]; then
    echo "Error: $DIST_DIR/fujinet not found after build" >&2
    exit 1
fi

# ── install ────────────────────────────────────────────────────────────────
# Copy only firmware files; preserve the user's fnconfig.ini and SD/.

echo ""
echo ">>> installing into $INSTALL_DIR ..."

cp -v "$DIST_DIR/fujinet"     "$INSTALL_DIR/fujinet"
cp -v "$DIST_DIR/run-fujinet" "$INSTALL_DIR/run-fujinet"
chmod +x "$INSTALL_DIR/run-fujinet"

# firmware resources (font, handlers, web UI, etc.)
rsync -a --delete "$DIST_DIR/data/" "$INSTALL_DIR/data/"

# shared libraries bundled by the dist target (Linux only)
find "$DIST_DIR" -maxdepth 1 -name '*.so*' -exec cp -v {} "$INSTALL_DIR/" \;

echo ""
echo "=== install complete ==="
echo "  binary  : $(file "$INSTALL_DIR/fujinet" | cut -d: -f2 | xargs)"
echo "  version : $("$INSTALL_DIR/fujinet" -V 2>&1 | head -1)"
echo ""
echo "Run with:"
echo "  cd $INSTALL_DIR && ./run-fujinet -c fnconfig.ini -s SD/"
