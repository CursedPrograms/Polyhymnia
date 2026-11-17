#!/bin/sh
set -e
cd "$(dirname "$0")"
DEST="${HOME}/.local/share/Polyhymnia"
mkdir -p "$DEST"
cp -r build/* "$DEST/" 2>/dev/null || cp build/Polyhymnia "$DEST/"
echo "Installed to $DEST"