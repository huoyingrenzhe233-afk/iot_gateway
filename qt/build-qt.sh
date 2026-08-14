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
    ssh "${BOARD_USER}@${BOARD_IP}" 'mkdir -p /root/qtapp'
    scp qt_gateway run.sh "${BOARD_USER}@${BOARD_IP}:/root/qtapp/"
    ssh "${BOARD_USER}@${BOARD_IP}" 'chmod +x /root/qtapp/qt_gateway /root/qtapp/run.sh'
fi
