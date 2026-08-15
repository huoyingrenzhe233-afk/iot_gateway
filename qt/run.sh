#!/bin/sh
set -eu

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/root/qtapp/lib}"
export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_QPA_PLATFORM_PLUGIN_PATH:-/root/qtapp/plugins}"
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-/usr/share/fonts/noto-sans-sc}"

HOST="${1:-127.0.0.1}"
PLATFORM="${2:-linuxfb}"

if [ "${PLATFORM}" = "wayland" ]; then
    if [ -z "${WAYLAND_DISPLAY:-}" ]; then
        for runtime_dir in "${XDG_RUNTIME_DIR:-}" /run/user/0 /run /tmp; do
            [ -n "${runtime_dir}" ] || continue
            socket_path="${runtime_dir}/wayland-0"
            if [ -S "${socket_path}" ]; then
                export XDG_RUNTIME_DIR="${runtime_dir}"
                export WAYLAND_DISPLAY="wayland-0"
                break
            fi
        done
    fi
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"
    export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
fi

exec ./qt_gateway "$HOST" -platform "$PLATFORM"
