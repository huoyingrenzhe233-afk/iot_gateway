#!/bin/bash
# 一键交叉编译 + 部署到板子(小白友好,无需手动设任何路径/环境变量)
#
# 用法:
#   ./build-arm.sh          # 只编译
#   ./build-arm.sh --deploy # 编译 + 传到板子 + 启动 + 验证 /api/health
#
# 工具链:/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu(干净,免环境变量)
# 端口:8081(板上 8080 被摄像头 mjpg_streamer 占用)

set -e
cd "$(dirname "$0")"

TC_FILE="$PWD/cmake/toolchain-linaro.cmake"
BOARD_IP=192.168.5.70
PORT=8081

echo "==> [1/4] 配置交叉编译(build-arm)"
cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE="$TC_FILE" >/dev/null

echo "==> [2/4] 编译"
cmake --build build-arm

echo "==> [3/4] 验证产物是 ARM64"
file build-arm/gateway | grep -q aarch64 || { echo "❌ 产物不是 aarch64!"; exit 1; }
echo "    ✅ aarch64 OK"

if [ "$1" = "--deploy" ]; then
    echo "==> [4/5] 部署到板子 $BOARD_IP"
    ssh -o BatchMode=yes root@$BOARD_IP "pkill -x gateway || true; sleep 1" 2>/dev/null
    scp -o BatchMode=yes build-arm/gateway root@$BOARD_IP:/root/gateway
    echo "==> [5/5] 传输配置文件(gateway.yaml + devices/ 设备注册表)"
    ssh -o BatchMode=yes root@$BOARD_IP "mkdir -p /root/config"
    scp -o BatchMode=yes config/gateway.yaml root@$BOARD_IP:/root/config/gateway.yaml
    scp -o BatchMode=yes -r config/devices root@$BOARD_IP:/root/config/  # 设备注册表(sensors/actuators.yaml)
    ssh -o BatchMode=yes root@$BOARD_IP "nohup /root/gateway $PORT > /root/gateway.out 2>&1 & sleep 1"
    echo "==> 验证 /api/health + /api/version + /api/devices:"
    ssh -o BatchMode=yes root@$BOARD_IP "wget -q -O- http://127.0.0.1:$PORT/api/health; echo; wget -q -O- http://127.0.0.1:$PORT/api/version; echo; wget -q -O- http://127.0.0.1:$PORT/api/devices; echo"
    echo ""
    echo "✅ 完成"
else
    echo ""
    echo "编译完成。用 ./build-arm.sh --deploy 可自动部署到板子。"
fi
