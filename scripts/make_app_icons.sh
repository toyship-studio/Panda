#!/bin/bash
# Generates the application icons from a single 1024×1024 PNG (macOS-only tools:
# sips + iconutil, Xcode is not needed). Run when the icon changes; results are kept in the repo:
#   native/OSX/<Name>.icns      — macOS (copied into Contents/Resources by the build)
#   native/Windows/<Name>.ico   — Windows (compiled in via .rc)
# Usage:         scripts/make_app_icons.sh <source-1024.png> <AppDir> <Name>
# Example:       scripts/make_app_icons.sh PandaEditor/native/AppIcon-1024.png PandaEditor PandaEditor
set -e

SOURCE="$1"
APP_DIR="$2"
NAME="$3"
if [ -z "$SOURCE" ] || [ -z "$APP_DIR" ] || [ -z "$NAME" ]; then
    echo "usage: $0 <source-1024.png> <AppDir> <Name>"
    exit 1
fi

WORK_DIR=$(mktemp -d)
ICONSET="$WORK_DIR/$NAME.iconset"
mkdir -p "$ICONSET"

# macOS .icns: size/@2x pairs following the iconutil convention.
for SIZE in 16 32 128 256 512; do
    DOUBLE=$((SIZE * 2))
    sips -z $SIZE $SIZE "$SOURCE" --out "$ICONSET/icon_${SIZE}x${SIZE}.png" > /dev/null
    sips -z $DOUBLE $DOUBLE "$SOURCE" --out "$ICONSET/icon_${SIZE}x${SIZE}@2x.png" > /dev/null
done
mkdir -p "$APP_DIR/native/OSX"
iconutil -c icns "$ICONSET" -o "$APP_DIR/native/OSX/$NAME.icns"

# Windows .ico: PNG-encoded entries (valid since Vista), packed by hand —
# without ImageMagick/dependencies.
ICO_DIR="$WORK_DIR/ico"
mkdir -p "$ICO_DIR"
for SIZE in 16 32 48 64 256; do
    sips -z $SIZE $SIZE "$SOURCE" --out "$ICO_DIR/$SIZE.png" > /dev/null
done
mkdir -p "$APP_DIR/native/Windows"
python3 - "$ICO_DIR" "$APP_DIR/native/Windows/$NAME.ico" <<'EOF'
import struct, sys, os
ico_dir, out_path = sys.argv[1], sys.argv[2]
sizes = [16, 32, 48, 64, 256]
images = [(s, open(os.path.join(ico_dir, f"{s}.png"), "rb").read()) for s in sizes]
with open(out_path, "wb") as f:
    f.write(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        dim = 0 if size == 256 else size  # 0 = 256 per the ICO format
        f.write(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for _, data in images:
        f.write(data)
EOF

rm -rf "$WORK_DIR"
echo "OK: $APP_DIR/native/OSX/$NAME.icns + $APP_DIR/native/Windows/$NAME.ico"
