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

### 🔍 mqtt_client 代码审查报告(2026-08-11,接入前必读)

> 审查对象:`src/core/common/mqtt/mqtt_client.h/.cpp`(用户草稿,已修到能编译)
> 方法:逐行核对 mongoose 7.20 源码,非推测
> 状态:**代码尚未接入 main.cpp**,以下问题在接入前处理

#### ✅ 已验证 OK(可放心)

- `c->fn_data` 传 `this` 给静态回调:查 `mg_connect_svc` 源码,确认 `c->fn_data = fn_data` ✅
- `publish` 里栈上 `opts` + 临时 `mg_str(topic.c_str())`:查 `mg_mqtt_pub` 源码,`mg_send` 是**同步拷贝**进发送缓冲,函数返回后 string 析构无悬垂 ✅
- `on_message` 回调参数已转 `std::string`(拷贝出接收缓冲区)✅
- `Logger` 单例有互斥锁,回调里 `LOG_*` 线程安全 ✅
- `on_message` 空调用有 `if` 保护 ✅

#### 🔴 高风险(接入前必须处理)

1. **生命周期:MqttClient 析构 vs 连接存活**
   - `event_handler` 经 `c->fn_data` 拿 `this`。若对象先销毁而连接还在 mongoose 循环 → 回调触发**野指针崩溃**
   - **要求**:MqttClient 生命周期 ≥ mgr 事件循环(main 栈上 + 退出前 `mg_mgr_free`,或堆分配常驻);**禁止**局部临时对象/拷贝

2. **断线不会自动重连(功能缺失)**
   - `reconnect_timer` 只声明未实现,`try_reconnect` 无人调用 → 掉线后永久断开
   - **修复**:`mg_timer_init(&mgr->timers, &timer_, 5000, MG_TIMER_REPEAT, reconnect_timer, this)` 周期调 `try_reconnect`(timer_ 需加为成员)

3. **topic 硬编码**:`"dev/mcu01/report"` 写死在 `handler_event`。config 里已有 `mqtt_topic_report`,应从 `Config` 传入(构造参数或 connect 参数)

#### 🟡 中风险

4. **CONNACK 未检查 ack**:`MG_EV_MQTT_OPEN` 的 `ev_data` 是 **`uint8_t*`(ack 码,0=成功)**——⚠️ mongoose.h 注释写 `int *` 但实现传 `&mm.ack`(uint8_t),**注释与实现不符,以实现为准**。当前不检查直接订阅;认证失败(ack≠0)时应记 ERROR 不订阅
5. **connect 可重复调用导致泄漏**:连续两次 `connect` 旧连接不断开 → 泄漏。connect 开头应 `if (conn) { conn->is_closing = 1; }` 或拒绝
6. **publish 的"假连接"**:`conn != nullptr` ≠ 已连接(CONNACK 未回)。此时 publish 数据进缓冲区,连接失败则丢弃——可接受但需知晓;严格做法用 `subscribed_` 当"已就绪"标志

#### 🔵 线程安全(专项结论)

- **mongoose 单线程事件循环**:所有 `mg_*` API 必须在 mgr 所在线程调用
- **当前安全**:`publish`/`on_message`/HTTP handler 都在同一事件循环线程
- **红线**:将来若给 `/api/control` 或设备轮询加**线程池**,别的线程直接调 `publish` 会破坏 mongoose 内部状态(发送缓冲无锁)→ 届时须用 wakeup/队列把任务投递回事件循环线程
- **`on_message` 回调在事件循环线程执行**:回调里**禁止阻塞**(卡死整个 HTTP+MQTT)、禁止长计算;只应做"拷贝数据→投递队列→立即返回"

#### ⚪ 死代码/小问题

- `subscribed_` 只写不读:要么删掉,要么 `publish` 前检查(见中风险 6)
- `reconnect_timer` 声明未定义:实现或删除
- `try_reconnect` 当前无人调用(配合高风险 2 解决)

#### 修复建议(按优先级)

1. 接入:main 里 `MqttClient mqtt;` 栈上创建,生命周期覆盖整个 mgr 循环;或堆分配常驻
2. 加定时器成员 `struct mg_timer timer_;` + `mg_timer_init` 5s REPEAT 调 `try_reconnect` → 实现自动重连
3. `connect` 增加参数或构造时传入 topic_report/topic_cmd(从 Config 读),去掉硬编码
4. `MG_EV_MQTT_OPEN` 里检查 `*(uint8_t*)ev_data == 0` 再订阅,否则 LOG_ERROR
5. `connect` 开头防重入:已连接则先关旧连接

### ⏳ 5.4 阶段三:MQTT 客户端 — **待执行(委派给 WSL AI)**
> 目标:网关作为 MQTT 客户端连板上 mosquitto,打通"网页 → 网关 → 单片机"控制链和"单片机 → 网关 → 网页"上报链。
> 参考:`third_party/mongoose.h` 2874-2917 行(MQTT API)。

**任务 1:MqttClient 模块**(放 `src/core/common/mqtt/` 或 `src/gateway/mqtt_client.cpp`,与现有结构统一)
- `mg_mqtt_connect(&mgr, "mqtt://127.0.0.1:1883", &opts, handler, NULL)` 连接
- broker 地址从 `Config` 读(`cfg.mqtt_broker`,已在 gateway.yaml 里)
- 事件回调处理:
  - `MG_EV_MQTT_OPEN`(CONNACK 成功)→ `mg_mqtt_sub` 订阅 `cfg.mqtt_topic_report`(`dev/mcu01/report`),打 LOG_INFO
  - `MG_EV_MQTT_MSG` → 收到上报,`LOG_INFO("MQTT recv %.*s: %.*s", ...)`,数据暂存全局(阶段四 /api/status 用)
  - `MG_EV_ERROR` / 连接断开 → LOG_WARN,尝试重连(简单方案:`mg_timer_add` 定时 5s 重连,或 poll 循环里判断 `c->is_closing`)
- 发布:`mqtt_publish(topic, message)` 封装 `mg_mqtt_pub`,qos=1

**任务 2:/api/control 下行**
- 路由 `POST /api/control`,body 为 JSON:`{"type":"control","body":{"led_on":true}}` 等
- 组信封:`{"type":"control","dev":"mcu01","ts":<unix时间戳>,"body":{...}}`(协议规范见第 1 节)
- 发布到 `cfg.mqtt_topic_cmd`(`dev/mcu01/cmd`),返回 `{"ok":true}`
- 需要 JSON 解析:mongoose 7.20 自带 `mg_json_get`(确认可用;yaml-cpp 不解析 JSON)

**任务 3:验证(本机 + 板上)**
- 本机:起 mosquitto(WSL 里 `sudo apt install mosquitto-clients` 或已装)→ 起 gateway → `mosquitto_pub -h 127.0.0.1 -t dev/mcu01/report -m '{"t":25}'` → 网关日志应出现 MQTT recv
- 板上:`./build-arm.sh --deploy` → `ssh root@192.168.5.70 "mosquitto_pub -h 127.0.0.1 -t dev/mcu01/report -m 'hello'"` → 网关日志(在 `/root/gateway.out` 或 gateway.log)出现接收记录
- 控制链:`curl -X POST http://127.0.0.1:8081/api/control -d '{"body":{"led_on":true}}'` → 板上 `mosquitto_sub -h 127.0.0.1 -t dev/mcu01/cmd -v` 应看到发布

**已知坑**:
- mongoose 7.20 `mg_mqtt_connect` 的 URL 格式:`mqtt://host:port`;`mg_mqtt_opts` 字段用 `mg_str("...")` 包裹
- `MG_EV_MQTT_MSG` 的 `struct mg_mqtt_message *msg` 用 `msg->topic`/`msg->data`(`.buf`/`.len`,不是 `.ptr`——老坑!)
- 板上 8081 端口测试用 `wget -q -O-`,无 curl
- 事件回调里**不要**做耗时操作;发布/订阅用 `mg_mqtt_pub`/`mg_mqtt_sub` 是异步的,不阻塞
- MQTT 断线重连是演示稳定性关键,至少要有重连逻辑(否则板上 mosquitto 重启网关就死了)

**验收标准**:
1. 本机:上报链(收)和控制链(发)都通,日志清晰
2. 板上:交叉编译部署后同样验证通过
3. 网关断线重连:kill 板上 mosquitto 再起,网关能自动重连并恢复订阅
4. 完成后提交分支 `feature/mqtt`(从最新 main 开出),更新本文档 5.4 为 ✅

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
