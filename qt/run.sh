#!/bin/sh
set -eu

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/root/qtapp/lib}"
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-/usr/share/fonts/noto-sans-sc}"

HOST="${1:-127.0.0.1}"
PLATFORM="${2:-linuxfb}"
exec ./qt_gateway "$HOST" -platform "$PLATFORM"
