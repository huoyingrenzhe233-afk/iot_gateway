# 交接文档 — RK3568 边缘智能网关

> 来源:Windows 侧 opencode 会话迁移(2026-08-11)
> 用途:在 WSL opencode 中无缝继续开发。读完本文即获得全部上下文。

## 1. 项目是什么

**基于 RK3568 的边缘智能网关控制系统**(大作业)。四端协作:

| 端 | 技术栈 | 负责人 | 目录 |
|---|---|---|---|
| 网关 | C++14 + CMake + mongoose 7.20 | 用户 | `src/` |
| 单片机 | STM32F103 + FreeRTOS + esp8266 AT | 同事 | `mcu/` |
| Web | HTML/JS | 同事 | `web/` |
| Qt | Qt5 | 同事 | `qt/` |

老师规范:`/home/kkk/Desktop/standard/project-plan.md`(Windows 桌面映射,见下)
API 清单:`/api/health` `/api/version` `/api/devices` `/api/actuators/:id/set` `/api/status` `/api/control` `/api/rules` `/api/camera/*` + `/ws`
协议信封:`{type, dev, ts, body}`;MQTT topic:`dev/mcu01/report`(上)/`dev/mcu01/cmd`(下)
控制 payload:`led_on/led_br/motor_on/motor_sp/motor_dir/buzzer`;设备固定 mcu01;zigbee 必做透传

## 2. 当前进度

### ✅ 阶段 0(环境)— 完成
- buildroot 编译烧录成功,板子系统自编译
- 板上已验证:SSH(dropbear)、mosquitto(改了 conf 加 `listener 1883 0.0.0.0` + `allow_anonymous true`)、USB 摄像头 mjpg-streamer(节点 `/dev/video9`,启动命令见下)
- 关键包已选:mosquitto / mjpg_streamer / ffmpeg / sqlite3 / dropbear / libyaml-cpp

### ✅ 阶段 1(网关骨架)— **本机 + 板上均验证通过(2026-08-11)**
- `src/gateway/main.cpp`:mongoose HTTP 服务,8080 端口(默认),`/api/health` 返回 `{"status":"ok"}`,未知路径 404
- `src/core/common/logger.h/.cpp`:自写日志器(单例 + 互斥锁 + 时间戳毫秒 + 控制台/文件双输出 + 级别过滤),`LOG_DEBUG/INFO/WARN/ERROR` 宏
- **实测结果(本机 x86)**:`/api/health` → HTTP 200;`/xxx` → HTTP 404;日志格式 `[2026-08-10 21:49:38.129][INFO][main.cpp:32] gateway starting`
- **板上实测(交叉编译版)**:`/api/health` → HTTP 200 `{"status":"ok"}`;`/xxx` → 404 ✅
- ⚠️ **端口冲突**:板上 8080 被 mjpg_streamer(摄像头)占用,`main.cpp` 已支持 `argv[1]` 传端口,板上用 `gateway 8081`

### ⏳ 未开始
- MQTT 客户端(阶段三 5.4)、设备管理、WebSocket、规则引擎、摄像头代理
- ✅ 已完成:config 加载 + `/api/version`(5.2/5.3)

## 3. 环境信息

| 项 | 值 |
|---|---|
| 板子 IP / 登录 | `192.168.5.70`,root,SSH 已配 RSA 免密(`~/.ssh/id_rsa_gw`),ed25519 不被板端 dropbear 接受 |
| SDK 路径 | `/home/kkk/source/rk356x_linux` |
| buildroot 版本 | **2018.02-rc3**(很老,无 spdlog 包) |
| buildroot 输出目录 | `buildroot/output/rockchip_rk3568`(**独立输出目录**,改配置后必须 `make savedefconfig`);⚠️ 该目录的 `host/` 工具链**不存在**,只剩 stub,勿用 |
| ✅ **交叉编译工具链(实际使用)** | `/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu`(Linaro 官方版,**干净免环境变量**,sysroot glibc 2.23 兼容板上 2.29) |
| ❌ 备选工具链 | `buildroot/output/rockchip_rk3568/host/bin/aarch64-...-gcc` 与 `prebuilts/gcc/.../gcc-buildroot-9.3.0-2020.03/`(**需 `export LD_LIBRARY_PATH=$TC/lib`,RPATH 指向构建者机器,已弃用**) |
| toolchainfile.cmake | ✅ `gateway/cmake/toolchain-linaro.cmake`(已写好,指向 /opt Linaro) |
| 项目仓库 | `/home/kkk/gateway`(git,远端 origin/main) |
| 摄像头启动 | `mjpg_streamer -b -i "input_uvc.so -d /dev/video9 -r 640x480 -f 15 -q 85" -o "output_http.so -p 8080"` |
| 摄像头型号 | USB CP-LL21A(识别 0bda:d327),OV5695 是 CSI 不可用 |
| 视频流 | `http://192.168.5.70:8080/?action=stream`(⚠️ 占用 8080,网关改 8081) |
| sysroot 已装第三方库 | yaml-cpp **0.5.2**(`usr/lib/libyaml-cpp.so.0.5.2`)+ Boost **1.63.0 头文件**(`usr/include/boost/`),均为 ARM/header-only,源码在 `/tmp/yaml-cpp-release-0.5.2/`、`/tmp/boost_1_63_0/` |
| 板上运行状态 | `/root/gateway 8081` + `/root/config/gateway.yaml`(config 目录结构 `/root/config/`)|

## 4. 已定的技术决策

1. **网关用 mongoose 7.20 单文件**(`third_party/mongoose.c/.h`,vendor 方式)。⚠️ 注意:`mg_str` 字段是 **`buf`** 不是 `ptr`(版本特性,踩过坑)
2. **日志:自写 logger 已落地并在项目里运行**(80 行,零依赖)。用户倾向 spdlog("不重复造轮子"的讨论已进行,原理已讲),但 **spdlog 未安装未测试**,且 buildroot 2018.02 无此包。**当前决策:先用自写 logger 推进,spdlog 待议**(走方案③:SDK 工具链手动交叉编译 + scp,不重烧镜像)
3. 配置用 yaml-cpp(**板上已装 0.5.2**,sysroot 已装同版本 + Boost 1.63.0 头文件),日志/配置等基础组件放 `src/core/common/`(注意:实际目录是 `src/core/common/logger/` 和 `src/core/common/config/`,带子目录)
4. 老师参考工程:`/home/kkk/Desktop/web_project/Iot-gateway/IotEdgeGateway`(只借鉴:file_logger.cpp / rule_engine.hpp / development.yaml)
5. Windows 桌面文档(教程 v2.0 等 3 份)在 `/mnt/c/Users/ThinkPad/Desktop/`(WSL 里直接读)

## 5. 下一步(按顺序)

### ✅ 5.1 交叉编译 + 上板验证 — **已完成(2026-08-11)**
- 工具链:`/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu`(SDK 的 buildroot/prebuilts 工具链有 RPATH 坑,已弃用)
- 一键脚本:`./build-arm.sh`(编译)/ `./build-arm.sh --deploy`(编译+部署+验证)
- 板上运行 `/root/gateway 8081`,`/api/health` → 200 `{"status":"ok"}` ✅

### ✅ 5.2 交叉编译 yaml-cpp 0.5.2 装进 linaro sysroot — **已完成(2026-08-11)**
> 结果:sysroot 已有 `include/yaml-cpp/yaml.h` + `lib/libyaml-cpp.so.0.5.2`(ARM),网关交叉编译通过,板上 `/api/version` → 200 `{"version":"1.0.0"}` ✅
> 三个坑(已解决,记录备查):
> 1. tag 名是 `release-0.5.2`(不是 `yaml-cpp-0.5.2`),用 `git ls-remote --tags` 确认
> 2. **yaml-cpp 0.5.2 严重依赖 Boost 头文件**(`boost/shared_ptr.hpp` 等,header-only)→ 需先装 Boost 1.63.0 头文件进 sysroot(`cp -r boost_1_63_0/boost $SYSROOT/usr/include/`,从 archives.boost.io 下载 97MB)
> 3. **共享库开关变量名是 `BUILD_SHARED_LIBS`**(不是 `YAML_BUILD_SHARED_LIBS`),且 CMakeLists 116 行 `find_package(Boost REQUIRED)` 需注释掉
> 源码位置:`/tmp/yaml-cpp-release-0.5.2/`(改过 CMakeLists 的补丁版)、`/tmp/boost_1_63_0/`

### ✅ 5.3 阶段二 — **已完成(2026-08-11)**
- `config/gateway.yaml`(server 端口 / mqtt broker / topic / 设备号 mcu01)+ `src/core/common/config/config.h/.cpp`(yaml-cpp 加载)— 已实现
- `/api/version` → `{"version":"1.0.0"}` — 已实现,**板上验证 HTTP 200** ✅
- ⚠️ 部署注意:程序启动时读 `config/gateway.yaml`(相对路径),**config 目录必须和 gateway 一起传到板上**(已部署到 `/root/config/gateway.yaml`);`build-arm.sh --deploy` **当前不传 config**,需手动 scp 或后续改进脚本
- 端口逻辑:`argv[1]` 优先于 config 里的 `server.port`;板上用 `8081` 避开摄像头的 8080

### 5.4 阶段三
- mongoose `mg_mqtt_connect` 连板上 mosquitto(127.0.0.1:1883)
- 订阅 `dev/mcu01/report`;`/api/control` → 组信封 → 发布 `dev/mcu01/cmd`

## 6. 已知的坑(别重复踩)

1. **buildroot 改配置后必须 `make savedefconfig`**,否则不生效;输出在独立目录 `output/rockchip_rk3568`
2. **重烧镜像会丢 mosquitto.conf 修改**(listener 1883 + allow_anonymous),要么重配要么以后做 rootfs overlay
3. **网络下载慢**:buildroot 包下载卡住时,Windows 浏览器手动下载丢进 `dl/`(文件名必须与 .mk 的 `*_SOURCE` 一致)
4. `mg_str` 用 `.buf` 不是 `.ptr`
5. 板子装新库优先考虑"不重烧"方案(手动交叉编译 scp),保住 mosquitto 配置
6. 交叉编译命令里注意:buildroot 工具链是 `aarch64-...`(rk3568 是 ARM64)
7. **SDK buildroot 输出目录的 host 工具链不存在**(只剩 stub),prebuilts 工具链有 RPATH 坑(`cc1` 报 `libisl.so.15` 缺失,需 `export LD_LIBRARY_PATH=$TC/lib`)。**直接用 `/opt` Linaro 工具链,零坑**
8. **板上 8080 被 mjpg_streamer 占用**(摄像头流),网关默认 8080 会 bind 失败。已支持 `argv[1]` 传端口,板上用 `8081`
9. **SSH 免密**:板端 dropbear 不接受 ed25519 密钥,须用 RSA 密钥(`~/.ssh/id_rsa_gw`)
10. **板上无 curl**,验证用 `wget -q -O- http://127.0.0.1:PORT/...`
11. **`pkill -f` 会误杀 ssh 会话自身**(命令行含目标字符串),用 `pkill -x gateway` 精确匹配
12. **yaml-cpp 0.5.2 真依赖 Boost 头文件**(header-only,非文档原以为的零依赖)→ 交叉编译前必须先把 boost 头文件装进 sysroot(详见 5.2)
13. **yaml-cpp 0.5.2 的共享库开关是 `BUILD_SHARED_LIBS`**(不是 `YAML_BUILD_SHARED_LIBS`,后者被静默忽略);CMakeLists 里 `find_package(Boost REQUIRED)` 需注释掉
14. **GitHub release tag 名**:yaml-cpp 是 `release-0.5.2`;boost 源码不在 github releases(404),用 `archives.boost.io` 下载
15. **板上有 libyaml-cpp.so.0.5.2**(buildroot 已装),网关交叉编译只需链接 sysroot 版本,运行时用板上库,无需往板上拷 .so

## 7. 从零开始:小白也能交叉编译(5 分钟)

> 完全不懂路径/库/环境变量也没关系,照抄下面 3 步即可。所有复杂的东西已被 `build-arm.sh` 封装。

### 7.1 一键方式(推荐,日常用)

```bash
cd /home/kkk/gateway
./build-arm.sh            # 只编译 → 产出 build-arm/gateway(ARM64 版)
./build-arm.sh --deploy   # 编译 + 传到板子 + 启动 + 自动验证 /api/health
```

### 7.2 手动拆解(理解原理用)

```bash
# ① 配置:告诉 CMake 用 /opt 的 ARM 翻译器(就是项目里那份 cmake/toolchain-linaro.cmake)
cd /home/kkk/gateway
cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-linaro.cmake

# ② 编译
cmake --build build-arm

# ③ 验证产物确实是 ARM64(不是 x86 白编译)
file build-arm/gateway        # 看到 "ARM aarch64" 就对了

# ④ 部署到板子
scp build-arm/gateway root@192.168.5.70:/root/gateway

# ⑤ 板上启动 + 验证(8081,避开摄像头的 8080)
ssh root@192.168.5.70 "pkill -x gateway; nohup /root/gateway 8081 > /root/gateway.out 2>&1 &"
ssh root@192.168.5.70 "wget -q -O- http://127.0.0.1:8081/api/health"   # 期待 {"status":"ok"}
```

### 7.3 解释:每一步在干嘛

| 命令 | 干什么 | 类比 |
|---|---|---|
| `cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=...` | 告诉"施工队长"用哪个翻译器 | 给工程队指派翻译 |
| `cmake --build build-arm` | 按图纸施工 | 盖楼 |
| `file build-arm/gateway` | 体检产物是不是 ARM 版 | 出厂质检 |
| `scp ... root@192.168.5.70:/root/` | 把成品送到板子 | 送盒饭到工地 |
| `ssh root@192.168.5.70 "..."` | 远程操作板子 | 打电话给工地 |

### 7.4 核心工具链(只需记住这一个路径)

```bash
# ARM 翻译器全家桶(编译器/链接器/库都在这里)
/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++

# 验证它存在且能用
/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++ --version
```

## 8. 常用命令速查

```bash
# 本机构建 + 测试
cd /home/kkk/gateway && cmake -B build && cmake --build build
./build/gateway &  # 然后 curl http://127.0.0.1:8080/api/health
tail -f gateway.log

# 交叉编译 + 部署(板上)
./build-arm.sh --deploy
# ⚠️ 注意:--deploy 不传 config,首次部署需手动补:
scp config/gateway.yaml root@192.168.5.70:/root/config/gateway.yaml
ssh root@192.168.5.70 "wget -q -O- http://127.0.0.1:8081/api/version"   # 板上验证 {"version":"1.0.0"}

# 板上完整手动部署(杀旧进程 → 传程序 → 启动 → 验证)
ssh root@192.168.5.70 "pkill -x gateway; sleep 1"
scp build-arm/gateway root@192.168.5.70:/root/gateway
ssh root@192.168.5.70 "mkdir -p /root/config"
scp config/gateway.yaml root@192.168.5.70:/root/config/gateway.yaml
ssh root@192.168.5.70 "nohup /root/gateway 8081 > /root/gateway.out 2>&1 & sleep 1; wget -q -O- http://127.0.0.1:8081/api/health"
```
