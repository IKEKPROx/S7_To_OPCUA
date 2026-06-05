#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/app"
RELEASE_APP="$APP_DIR/build/macos/Build/Products/Release/S7ToOPCUA.app"
DIST_DIR="$ROOT_DIR/dist"
DIST_APP="$DIST_DIR/S7ToOPCUA.app"
RUNTIME_DIR="$DIST_APP/Contents/Resources/gateway_runtime"
RUNTIME_LIB_DIR="$RUNTIME_DIR/lib"

FLUTTER_BIN="${FLUTTER_BIN:-/Users/impxssive./development/flutter/bin/flutter}"

echo "Building Flutter macOS app..."
echo "Building gateway runtime tools..."
(cd "$ROOT_DIR" && make gateway fake_plc ua_monitor)
(cd "$APP_DIR" && "$FLUTTER_BIN" build macos)

echo "Preparing distributable app..."
rm -rf "$DIST_APP"
mkdir -p "$DIST_DIR"
cp -R "$RELEASE_APP" "$DIST_APP"

mkdir -p "$RUNTIME_LIB_DIR"
cp "$ROOT_DIR/gateway" "$RUNTIME_DIR/gateway"
cp "$ROOT_DIR/fake_plc" "$RUNTIME_DIR/fake_plc"
cp "$ROOT_DIR/ua_monitor" "$RUNTIME_DIR/ua_monitor"
cp -R "$ROOT_DIR/config" "$RUNTIME_DIR/config"
cp -L "$ROOT_DIR/third_party/snap7/lib/libsnap7.dylib" "$RUNTIME_LIB_DIR/libsnap7.dylib"
cp -L "/opt/homebrew/opt/cjson/lib/libcjson.1.dylib" "$RUNTIME_LIB_DIR/libcjson.1.dylib"
cp -L "/opt/homebrew/opt/open62541/lib/libopen62541.1.5.dylib" "$RUNTIME_LIB_DIR/libopen62541.1.5.dylib"

chmod +x "$RUNTIME_DIR/gateway" "$RUNTIME_DIR/fake_plc" "$RUNTIME_DIR/ua_monitor"
chmod +w "$RUNTIME_DIR/gateway" "$RUNTIME_DIR/fake_plc" "$RUNTIME_DIR/ua_monitor" "$RUNTIME_LIB_DIR/"*.dylib

echo "Rewriting dynamic library paths..."
install_name_tool -id "@rpath/libsnap7.dylib" "$RUNTIME_LIB_DIR/libsnap7.dylib"
install_name_tool -id "@rpath/libcjson.1.dylib" "$RUNTIME_LIB_DIR/libcjson.1.dylib"
install_name_tool -id "@rpath/libopen62541.1.5.dylib" "$RUNTIME_LIB_DIR/libopen62541.1.5.dylib"

install_name_tool -add_rpath "@executable_path/lib" "$RUNTIME_DIR/gateway" 2>/dev/null || true
install_name_tool -change "/opt/homebrew/opt/cjson/lib/libcjson.1.dylib" "@rpath/libcjson.1.dylib" "$RUNTIME_DIR/gateway"
install_name_tool -change "/opt/homebrew/opt/open62541/lib/libopen62541.1.5.dylib" "@rpath/libopen62541.1.5.dylib" "$RUNTIME_DIR/gateway"

install_name_tool -add_rpath "@executable_path/lib" "$RUNTIME_DIR/fake_plc" 2>/dev/null || true

install_name_tool -add_rpath "@executable_path/lib" "$RUNTIME_DIR/ua_monitor" 2>/dev/null || true
install_name_tool -change "/opt/homebrew/opt/cjson/lib/libcjson.1.dylib" "@rpath/libcjson.1.dylib" "$RUNTIME_DIR/ua_monitor"
install_name_tool -change "/opt/homebrew/opt/open62541/lib/libopen62541.1.5.dylib" "@rpath/libopen62541.1.5.dylib" "$RUNTIME_DIR/ua_monitor"

echo "Signing app locally..."
codesign --force --sign - "$RUNTIME_LIB_DIR/libsnap7.dylib"
codesign --force --sign - "$RUNTIME_LIB_DIR/libcjson.1.dylib"
codesign --force --sign - "$RUNTIME_LIB_DIR/libopen62541.1.5.dylib"
codesign --force --sign - "$RUNTIME_DIR/gateway"
codesign --force --sign - "$RUNTIME_DIR/fake_plc"
codesign --force --sign - "$RUNTIME_DIR/ua_monitor"
codesign --force --deep --sign - "$DIST_APP"

echo "Packaged app:"
echo "$DIST_APP"
