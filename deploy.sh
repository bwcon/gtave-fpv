#!/bin/sh
# Copies the built mod into the game folder. Run from the repo root.
set -e
GAME="$(cd "$(dirname "$0")/../GTAIV" && pwd)"
# The loader is locked while the game runs; it only changes rarely.
cp build/GTAIV.FPV.asi "$GAME/" 2>/dev/null || echo "loader in use, kept the running one"
# The running game hot-reloads the core within a second of this copy.
cp build/GTAIV.FPV.core.dll "$GAME/"
# LIBFPV_DLL=~/Projects/libfpv/build/win32-kamikaze/libfpv.dll ./deploy.sh for a kamikaze build.
cp "${LIBFPV_DLL:-$HOME/Projects/libfpv/build/win32/libfpv.dll}" "$GAME/" 2>/dev/null || echo "libfpv.dll in use, kept the running one"
echo "deployed to $GAME"
