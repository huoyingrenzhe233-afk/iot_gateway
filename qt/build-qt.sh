#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_DIR="${QT_DIR:-/home/kkk/qt-aarch64}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin}"
BOARD_IP="${BOARD_IP:-192.168.137.198}"
BOARD_USER="${BOARD_USER:-root}"
SSH_KEY="${SSH_KEY:-${HOME}/.ssh/id_rsa_gw}"
SSH_OPTS=()
if [[ -f "${SSH_KEY}" ]]; then
    SSH_OPTS+=("-i" "${SSH_KEY}")
fi

export PATH="${TOOLCHAIN_DIR}:${PATH}"
cd "${ROOT_DIR}/qt"
"${QT_DIR}/bin/qmake" gateway.pro
make -j"${JOBS:-2}"
file qt_gateway

if [[ "${1:-}" == "--deploy" ]]; then
    ssh "${SSH_OPTS[@]}" "${BOARD_USER}@${BOARD_IP}" \
        'mkdir -p /root/qtapp/lib /root/qtapp/plugins/platforms'
    scp "${SSH_OPTS[@]}" qt_gateway run.sh "${BOARD_USER}@${BOARD_IP}:/root/qtapp/"
    tar -C "${QT_DIR}/lib" -chf - \
        libQt5Core.so.5 libQt5Gui.so.5 libQt5Network.so.5 libQt5Widgets.so.5 \
        | ssh "${SSH_OPTS[@]}" "${BOARD_USER}@${BOARD_IP}" \
            'tar -C /root/qtapp/lib -xhf -'
    tar -C "${QT_DIR}/plugins" -chf - platforms/libqlinuxfb.so \
        | ssh "${SSH_OPTS[@]}" "${BOARD_USER}@${BOARD_IP}" \
            'tar -C /root/qtapp/plugins -xhf -'
    ssh "${SSH_OPTS[@]}" "${BOARD_USER}@${BOARD_IP}" \
        'chmod +x /root/qtapp/qt_gateway /root/qtapp/run.sh'
fi
