#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_DIR="${QT_DIR:-/home/kkk/qt-aarch64}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin}"
BOARD_IP="${BOARD_IP:-10.137.31.9}"
BOARD_USER="${BOARD_USER:-root}"

export PATH="${TOOLCHAIN_DIR}:${PATH}"
cd "${ROOT_DIR}/qt"
"${QT_DIR}/bin/qmake" gateway.pro
make -j"${JOBS:-2}"
file qt_gateway

if [[ "${1:-}" == "--deploy" ]]; then
    scp qt_gateway "${BOARD_USER}@${BOARD_IP}:/tmp/qt_gateway"
    ssh "${BOARD_USER}@${BOARD_IP}" 'chmod +x /tmp/qt_gateway && /tmp/qt_gateway 127.0.0.1 -platform linuxfb'
fi
