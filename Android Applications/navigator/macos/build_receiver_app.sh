#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
APP_DIR="$SCRIPT_DIR/NaviJsonMac.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"

rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR"

swiftc "$SCRIPT_DIR/BleNaviReceiver.swift" \
  -framework Foundation \
  -framework CoreBluetooth \
  -o "$MACOS_DIR/NaviJsonMac"

cp "$SCRIPT_DIR/NaviJsonMac-Info.plist" "$CONTENTS_DIR/Info.plist"
chmod +x "$MACOS_DIR/NaviJsonMac"

echo "Built: $APP_DIR"
echo "Run with: open \"$APP_DIR\""
echo "Log file: ~/Desktop/NaviJsonMac.log"
