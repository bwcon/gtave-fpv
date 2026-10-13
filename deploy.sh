#!/bin/sh
# Copies the built mod into the game folder. Run from the repo root.
set -e
GAME="$(cd "$(dirname "$0")/../GTAIV" && pwd)"
cp build/GTAIV.FPV.asi "$GAME/"
cp "$HOME/Projects/libfpv/build/win32/libfpv.dll" "$GAME/"
echo "deployed to $GAME"
