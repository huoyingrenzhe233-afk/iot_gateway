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
控制 payload:`led_on/led_br/motor_on/motor_sp/motor_dir/buzzer`;设备固定 mcu01

**硬件设备清单(2026-08-12 用户确认,共 6 个)**:
| 设备 | 类型 | 上报/控制 |
|---|---|---|
| LED 灯 | 执行器 | 控制(led_on/led_br) |
| 温湿度传感器 | 传感器 | 上报(temp/humi) |
| 蜂鸣器 | 执行器 | 控制(buzzer) |
| 光敏传感器 | 传感器 | 上报(light) |
| 红外传感器 | 传感器 | 上报(ir) |
| 风扇电机 | 执行器 | 控制(motor_on/motor_sp/motor_dir) |

**📡 双通道通信设计(2026-08-12 用户澄清,重要!)**
- 网关 ↔ 单片机之间**有两种可切换的通信通道**:① MQTT(WiFi/以太网)② ZigBee(无线模块)
- **Web/Qt 界面有"切换按钮"**,选择当前用哪个通道(老师方案的通道冗余设计)
- 通道切换 = 网关侧抽象层(`Channel` 接口,策略模式):`send_to_mcu(msg)` 内部判断当前通道 → MQTT 发布 或 ZigBee 发送
- **⚠️ 待确认(硬件前提)**:RK3568 板子是否有 zigbee 模块?(`ls /dev/ttyUSB* /dev/ttyS* /dev/ttyACM*`);老师 plan.md 中 zigbee 通道的具体 API/协议描述
- **⚠️ 待确认**:zigbee 通道是"真做"(网关直连 zigbee 设备/协调器)还是"简化"(单片机侧 zigbee,网关只走 MQTT)?
- **当前策略:先做扎实 MQTT 通道(阶段三已完成),zigbee 通道等硬件/老师要求确认后再设计抽象层**

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

### ✅ 阶段 2(配置加载)— 完成(2026-08-11)
- `config/gateway.yaml` + `src/core/common/config/`(yaml-cpp 加载)+ `/api/version` → `{"version":"1.0.0"}`(本机 + 板上验证)

### ✅ 阶段 3(MQTT 客户端)— 完成(2026-08-12,本机全链路验证)
- `src/core/common/mqtt/`:`MqttClient`(连接/订阅/发布/断线重连)+ `/api/control` 下行(control.cpp 组信封)
- 控制链 + 上报链双向真实 MQTT 闭环验证(独立 mosquitto_sub/pub 捕获证据)

### ✅ 阶段 4(设备状态管理)— 完成(2026-08-12,本机全链路验证)
- `src/core/device/`:`Device` 状态缓存 + `DeviceRegistry` 注册表 + /api/status + /api/devices
- 上报/回执/控制三链验证通过;部分字段下发修复(详见 5.5)

### ✅ 阶段 7(规则引擎)— 完成(2026-08-13,本机全链路 + 真 mosquitto 端到端验证)
- `src/core/rules/`:`RuleEngine`(yaml 加载/校验/边沿触发评估/启停/热重载)+ `/api/rules` 系列 4 个 API
- 详见 5.7 节;P1 设计决策(映射表方案 A)已定稿落地

### ✅ 阶段 8(摄像头接口)— 完成(2026-08-13,本机 + 板上真机验证)
- `src/core/camera/`:`CameraManager`(mjpg-streamer 推流 / wget 抓拍 / ffmpeg -c copy 录像,零转码)+ 5 个 `/api/camera/*` 路由 + 前端页面伺服
- 前端 `web/index.html`(方案 C:全链路 MJPEG);详见 5.8 节

### ⏳ 未开始
- WebSocket /ws、/api/devices/:id、feature/device 开 PR 合 main

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
6. **双通道决策(2026-08-12)**:网关↔单片机支持 MQTT/ZigBee 双通道切换(Web/Qt 有切换按钮)。**当前只做 MQTT 通道(已通);zigbee 通道待硬件确认后,用 Channel 抽象层(策略模式)实现**——`send_to_mcu(msg)` 内部按当前通道分发。不提前过度设计

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

### 🔍 mqtt_client 代码审查报告(2026-08-11)— ✅ 问题已全部修复(2026-08-12)
> 历史审查记录,保留备查;所有问题已按修复建议处理完毕(见 5.4 阶段三完成状态)

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

### ✅ 5.4 阶段三:MQTT 客户端 — **已完成(2026-08-12,本机全链路验证通过)**
> 任务 1(MqttClient)✅;**任务 2(/api/control)✅ 本机真实 MQTT 闭环验证**;任务 3 本机 ✅(板上验证见 5.5)
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

### ✅ 5.5 阶段四:设备状态管理 + 注册表 — **已完成(2026-08-12,本机全链路验证)**
> 目标:单片机上报 → 网关缓存 → 前端轮询 /api/status;设备清单 → /api/devices

**功能**:
- ✅ `src/core/device/device.{h,cpp}`:6 外设状态缓存
  - `update_from_report`:解析 sensor 上报(4 传感器)+ status 回执(items 数组,长短名兼容)
  - `update_from_control`:控制命令缓存(UI 秒响应,回执校准;支持部分字段下发)
  - `get_status_json`:/api/status 的 10 字段聚合(4 传感器字符串 + 6 执行器数值)
- ✅ `src/core/device/device_registry.{h,cpp}`:静态登记表,读 config/devices/*.yaml
  - sensors.yaml(4 传感器:temp_1/humi_1/light_1/ir_1)+ actuators.yaml(3 执行器:led_1/motor_1/buzzer_1)
  - `json_escape` 转义 + `kind` 校验(sensor/actuator)
- ✅ main.cpp 集成:/api/status + /api/devices 路由;on_message → update_from_report;HttpContext 加 device/registry 指针(fn_data)
- ✅ 实测:上报 → /api/status 更新;回执 → 执行器校准;控制 → 缓存秒响应;部分字段下发保持其他字段;交叉编译 aarch64 ✅

**已解决的解析坑(记录备查)**:
- type 用 `mg_json_get_str`(get_tok 带引号);数字值用 `mg_json_get_num`(get_str 只认字符串)
- 回执遍历越界用 `mg_json_get_tok` 节点存在判断(不能用 -1,与合法值冲突)
- 部分字段下发用 tok 存在性判断(不能用默认值 0,会重置其他字段)
- `%g` 会把 55.0 显示成 55(可接受;若严格要求小数位改固定格式)

**一致性铁律**:规则引擎 yaml 的 sensor_id/actuator_id 必须与 devices yaml 的 id 一字不差(教程 5.2.8 强调)

### ✅ 5.7 阶段七:规则引擎 — **已完成(2026-08-13,本机全链路 + 真 mosquitto 端到端验证)**

> 老师验收 20 分项;唯一"运行时可动态改"的配置。设计决策由用户拍板:映射表方案 A、单条件、演示场景自定。

**文件**:
- 新增 `src/core/rules/rule_engine.h/.cpp`(452 行):`Rule` 结构体 + `RuleEngine` 类
- 新增 `config/rules/rules.yaml`:4 条演示规则(高温报警/解除 + 暗光开灯/亮光关灯,两对成对规则)
- `device_registry` 加 `contains(id, kind)`(规则校验的权威来源)
- `main.cpp` 接入:HttpContext.rules、启动加载、on_message 里 `evaluate(payload)`、`on_action` 回调(publish + update_from_control)、4 条路由
- `CMakeLists.txt` 加 rule_engine.cpp;`build-arm.sh --deploy` 已加 `config/rules/` 同步 + `/api/rules` 验证

**核心机制**:
1. **边沿触发(上升沿)**:条件从"不满足→满足"的瞬间才下发一次动作;持续满足不重复下发(防 2s 上报刷爆 MQTT)。恢复规则(如 temp_alarm_off)负责回退。**首次上报即满足会立即触发一次(初始状态同步,设计行为非 bug)**
2. **加载校验(防哑弹)**:id 非空不重复;sensor_id 在映射表+注册表;op ∈ {> < >= <= ==};actuator_id 在映射表+注册表;field 在执行器字段白名单。非法规则 LOG_WARN 跳过
3. **热重载 reload**:重读 yaml 整体替换;保留同名规则的运行时 enabled 状态;解析失败保留旧规则
4. **enable/disable**:运行时内存标志,API 改,reload 不丢
5. **映射表(方案 A 落地)**:传感器 4 条(temp_1→$.body.data.temp 等)+ 执行器 3 条白名单(led_1→{led_on,led_br} 等)

**API(实测通过)**:
- `GET /api/rules` → `[{"id","name","enabled","when":{sensor,op,value},"then":{actuator,field,value}}]`
- `POST /api/rules/reload` → `{"ok":true}` / `{"ok":false,"message":"reload failed"}`
- `POST /api/rules/:id/enable|disable` → 200 `{"ok":true}` / 404 `{"ok":false,"message":"rule_not_found"}`
- 路由顺序铁律:reload 必须排在 :id/enable、:id/disable 之前(否则 reload 被误解析为 id)

**验证证据**:
- 单元测试 21 项全过(边沿/恢复/停用/垃圾输入/非法规则/重载语义)
- 真 mosquitto 端到端:mosquitto_pub 发 temp=35 → 网关下发 `{"body":{"buzzer":1}}` ✓;temp=36 无重复 ✓;temp=28 → `{"body":{"buzzer":0}}` ✓
- curl 实测 4 个 API + /api/status、/api/devices 回归 ✓

**规则触发数据流**:`sensor 上报 → on_message → update_from_report(缓存) + evaluate(规则) → 边沿满足 → fire 组 cmd 信封 → on_action → publish(dev/mcu01/cmd) + update_from_control(缓存同步)`

### ✅ 5.8 摄像头接口 — **已完成(2026-08-13,方案 C:全链路 MJPEG,零转码)**

> 决策由用户拍板:**最稳路线 = 老师 plan.md 的 mjpg-streamer 参考实现**,不用 HLS/h264_rkmpp(RK3568 MPP 栈不稳定 + 延迟大)。前端 HTML 可改。

**⚠️ API 名字以老师给的 `standard/index.html` 为准**(与 plan.md 表格不同):`start_stream`/`stop_stream`/`start_record`/`stop_record`/`snapshot`(plan.md 写的是 start/stop/record/start/record/stop)。网关**GET/POST 都接受**(HTML 用 GET,plan.md 写 POST)。

**文件**:
- 新增 `src/core/camera/camera_manager.h/.cpp`:`CameraManager` 类,fork+exec 管理子进程
- `config` 加 `camera.device`(`/dev/video9`)/`camera.port`(8080);`main.cpp` 加 5 个路由 + `/` 静态伺服 + 僵尸收割定时器;`build-arm.sh` 加 web/ 部署
- 前端:`web/index.html`(复制自 standard,改造 HLS→MJPEG)

**三个功能(全部零转码)**:
| 功能 | 实现 |
|---|---|
| 推流 | fork `mjpg_streamer`(input_uvc.so 读摄像头 → output_http.so 出 MJPEG 到 8080);stop 时 SIGTERM+waitpid |
| 抓拍 | fork `wget -q -O snapshots/snapshot_<ts>.jpg http://127.0.0.1:8080/?action=snapshot`(阻塞 waitpid ~0.2s) |
| 录像 | fork `ffmpeg -y -i http://127.0.0.1:8080/?action=stream -c copy records/record_<ts>.avi`(纯拷贝不编码) |

**进程管理约定**:
- `spawn()`:fork;子进程关 [3,_SC_OPEN_MAX) fd + stdout/stderr→/dev/null + execvp(失败 _exit(127))
- 僵尸收割:main 里 `mg_timer_add` 5s REPEAT 调 `waitpid(-1, WNOHANG)`(reap_children)
- 前端 `<img src="http://<host>:8080/?action=stream">` 直连流,网关不代理视频数据

**接口(实测)**:
- `GET|POST /api/camera/start_stream|stop_stream|start_record|stop_record` → `{"ok":true}` / 500 `{"ok":false,"message":...}`
- `GET|POST /api/camera/snapshot` → `{"ok":true,"filename":"snapshot_<ts>.jpg"}` / 500
- `GET|POST /api/camera/status` → `{"running":bool,"recording":bool}`
- `GET /`、`GET /index.html` → 伺服 `web/index.html`(浏览器开 `http://<板子IP>:8081/`)

**已知限制**:`start_stream` 返回的是"fork 成功",不校验 mjpg_streamer 是否真在 8080 伺服(摄像头没插 → input_uvc 打开失败 → 进程即退,但 API 仍回 ok)。演示前确认摄像头已插;`/api/camera/status` 可查真实运行态。

**待验证(🟡)**:板上真机——插 USB 摄像头(0bda:d327)→ deploy → 浏览器开 8081 页面点"开始推流"看实时画面 + 抓拍 + 录像。

> ✅ **板上真机已验证(2026-08-13)**:插摄像头后全链路测通——start_stream 真起 mjpg_streamer(8080 出流 38552B JPEG)、API 抓拍存 49640B jpg、录像 3s 存 1.7MB avi、status 正确反映 running/recording、stop_stream 进程正确退出。

### ⏳ 5.6 待办清单(按优先级)

| 优先级 | 事项 | 说明 |
|---|---|---|
| ✅ 高 | **规则引擎(20 分)** | **已完成(2026-08-13)**,详见 5.7 |
| ✅ 高 | **摄像头接口** | **已完成(2026-08-13)**,方案 C 详见 5.8 |
| ✅ 中 | 板上验证阶段三/四 | **已完成(2026-08-13,P0)** |
| ✅ 中 | 板上验证 MQTT 心跳修复 | **已完成(2026-08-13,P0)**:空闲期不再周期性断连 |
| ✅ 中 | 板上验证规则引擎 | **已完成(2026-08-13)**:板上实测 T2/T3/T4 全过 |
| 🟡 中 | 板上验证摄像头 | **已完成(2026-08-13)**:插摄像头后真机测通推流/抓拍/录像(见 5.8 验证证据) |
| 🟡 中 | `/api/devices/:id` 单设备详情 | 验收标准第 2 条,简单补 |
| 🟡 中 | WebSocket /ws | 实时推送(替代轮询);消息格式见协议定稿 |
| 🟢 低 | feature/device 开 PR 合 main | 阶段四落袋 |

> P0(2026-08-13)已完成:6 个提交已 push;板上部署验证通过(含 MQTT 心跳修复)。P1(规则引擎)、P2(摄像头)均已完成本机/板上验证,见 5.7/5.8。

**⏳ 待定义:规则引擎的 id → MQTT 字段路径映射(写规则引擎前必做)**
- 现状:两套标识并存——注册表 id(`temp_1`/`led_1`...)vs MQTT 字段(`$.body.data.temp`/`body.led_on`)
- 规则引擎 yaml 引用 sensor_id/actuator_id(注册表 id),取值需对应 MQTT 字段路径——**目前无映射**
- 两种解法(二选一):A. 加映射表(7 条);B. 规则直接引用 MQTT 字段名(简单,偏离"规则引用设备 id"设计)

> ✅ **已定稿(2026-08-13,方案 A)**:`rule_engine.cpp` 内两张静态映射表——传感器 id→上报 JSON 路径(4 条)+ 执行器 id→允许命令字段白名单(3 条)。规则加载时双重校验:映射表 + 注册表 `contains()`。详见 5.7 节。

### ✅ 2026-08-12 全代码审查(最终轮)

> 审查范围:全部 13 个源文件(config/logger/mqtt/control/device/registry/main),编译通过

**结论:全部通过 ✅**(config/logger/mqtt/device/device_registry/main 均无新问题)

| 发现 | 处理 |
|---|---|
| control.h 第 15 行注释还写 `body`(代码已是 payload) | ✅ 已修注释 |
| logger 线程安全(级别检查在锁内) | ✅ 已确认(之前修过) |
| device 部分字段/回执遍历 | ✅ 已确认(之前修过) |
| registry json_escape/kind 校验 | ✅ 已确认(之前修过) |

**当前代码状态**:13 文件 1196 行,全部功能本机实测通过,交叉编译 aarch64 通过,无已知 bug。

### ✅ 2026-08-13 全代码审查 + 修复轮(本机验证全部通过)

> 范围:全部源文件重读 + `cmake --build build` 通过 + LSP 0 诊断。上一轮(08-12)结论"无已知 bug"已被本轮更新。

**已修复(4 项代码 + 1 项稳健性 + 1 项重大 MQTT 问题)**:

| # | 文件 | 修复 |
|---|---|---|
| 1 | `config.h` | `mqtt_broker` 默认值补 `mqtt://` 前缀(与 gateway.yaml 对齐;yaml 缺失时默认值也能连) |
| 2 | `main.cpp` | 4 个 GET 端点(`/api/version` `/api/health` `/api/devices` `/api/status`)加 `mg_match(method, "GET")` 校验,非 GET 落 404 |
| 3 | `mqtt_client.cpp` | `mg_mqtt_pub` 返回值检查:0=失败 `LOG_ERROR`,成功带 packet_id `LOG_INFO`(注:mongoose 实现极晚期失败可能仍返回非 0,按文档契约判 0) |
| 4 | `control.cpp` | include 尖括号 → 双引号,风格统一 |
| 5 | `device.cpp` | status 回执循环重构(详见下) |
| 6 | `mqtt_client.h/.cpp` | **MQTT keepalive 心跳修复**(详见下,解决周期性断连) |

**用户明确不做的(非商业项目)**:`/api/control` 鉴权、日志注入过滤——已讨论,无需处理。

**status 回执循环重构(device.cpp)**
- 原逻辑:用 `items[i].value` 存在性当终止条件 → 把"数组到头"和"某项缺 value"两件事混为一谈。某项缺 value → 误判数组结束 → **后续所有项被静默丢弃**
- 新逻辑两步分离:① `items[i]` 本身不存在 → break(数组到头);② value 缺失 → `LOG_WARN` + continue(只跳过该项)
- 附带好处:缺 value 的项被跳过(状态保持原值),不会被 `mg_json_get_long` 默认值 0 误清零
- A/B 验证:旧版中间项缺 value 时 `motor_on` 丢失(0)FAIL;新版 `motor_on=1` PASS;正常报文行为不变
- 循环上限 16 的语义:防御性保险丝(防终止条件失效死循环),**不是设计循环次数**;正常数组 N 项只跑 N+1 次(多一次探测数组末尾)

**🔴 MQTT 周期性断连根因与修复(重大)**
- 现象(板上日志):空闲期每 ~95s 断连一次,5s 后自动重连,循环往复
- 根因链:① `opts.keepalive = 60` 只写进 CONNECT 报文(承诺),② mongoose **不自动发 PINGREQ**(全库仅 `mg_mqtt_ping()` 一处,须应用自己定时调用),③ 网关从未调用 → broker 按 MQTT 3.1.1 规范在 **1.5×60=90s** 无消息强制断开 → 5s 重连定时器接回
- 为什么平时没暴露:单片机持续上报时每条 report 都重置 broker 的 90s 计时器;**只有空闲期**触发
- 真实影响:断线 ~5s 窗口内 `/api/control` 命令被 `publish()` 丢弃(`conn==nullptr` 防御检查,不崩溃但丢命令)
- 为什么不会崩:MQTT 星型架构——上报方向(单片机→broker→网关)在网关掉线时消息在 broker 处丢失(`clean=true` 不缓存),网关无代码执行;下行方向 `publish()` 有 `conn==nullptr||!subscribed_` 防御检查
- **修复**:新增常驻心跳定时器 `ping_timer_`(30s REPEAT = keepalive/2),`conn != nullptr` 时调 `mg_mqtt_ping()`
- 验证:Python 假 broker 端到端测试(真实 MQTT 协议)→ 连接后 **30.0s 整收到 PINGREQ**,PASS;`cmake --build build` 通过

**clangd 假错修复(工具链)**
- 根因(实测逐场景验证,非推测):① 系统 clangd 10.0.0 **完全不读 `.clangd` 配置文件**(写 `Suppress:'*'` 都无效);② clangd 自动发现编译数据库时**只在源文件父目录链找 `compile_commands.json`,不进 `build/` 子目录** → 永远找不到 → 无 include 路径 → 连环假错;③ 新文件(未编译)无编译参数 → 同样假错
- 修复:`CMakeLists.txt` 加 `copy_compile_commands` 目标(构建时复制 `build/compile_commands.json` 到项目根目录;**仅宿主构建**,`CMAKE_CROSSCOMPILING` 时跳过)+ 显式 `CMAKE_CXX_STANDARD 14`(编译命令带 `-std=gnu++14`);`.gitignore` 加 `/compile_commands.json`;`.clangd` 升级新格式(加 `CompileFlags.Add` 兜底标志,clangd 12+ 生效)
- 结果:LSP 诊断 **68 条假错 → 0**;clangd 10 零配置裸启动全部文件 0 诊断;新文件场景 0 诊断
- 可选升级:`sudo apt install clangd-12` + `update-alternatives`(升级后 `.clangd` 配置才生效,不升级也不影响,根目录 DB 方案对老版本同样有效)

**git 状态提醒**:本轮改动(src 修复 + CMakeLists + .gitignore)均未提交;`build-arm.sh` 有一处用户自己的未提交改动(部署时同步 `config/devices/` + 验证 `/api/devices`),提交前注意区分。

### ✅ 2026-08-13 全代码审查(规则引擎 + 摄像头新增代码)

> 范围:新增 rule_engine(452 行)、camera_manager(264 行)、main.cpp 集成(484 行),编译通过

**结论:新增代码质量高,无功能性 bug ✅**

| 文件 | 评价 |
|---|---|
| rule_engine.h/.cpp | ✅ 优秀:映射表(方案 A 落地)、双重校验(映射表+注册表)、边沿触发、热重载保留 enabled、json_escape |
| camera_manager.h/.cpp | ✅ 优秀:fork+execvp(非 system 防注入)、子进程关 fd、僵尸收割、wget/ffmpeg 零转码 |
| main.cpp 集成 | ✅ 规范:规则/摄像头接入 HttpContext、on_action 回调、reap_children 定时器 |

**修复(注释过期,非 bug)**:
- main.cpp 头部注释"阶段 1-3"→"阶段 1-8 全部集成";数据流注释去掉"WS"(未实现)

**当前代码全貌**:15 源文件 + main.cpp,覆盖阶段 0-8(环境/骨架/配置/MQTT/设备/注册表/规则引擎/摄像头),全部本机验证通过,规则引擎+摄像头板上真机验证通过。

### 📡 通信协议定稿(2026-08-11 盘点,唯一权威)

> 来源:桌面《项目实现教程-v2.0.md》第 5 章(v1.0/开发流程指南里的命令表已过时,以本表为准)

**MQTT topic**:
```
上行(单片机→网关): dev/mcu01/report    # 传感器数据/状态上报, QoS 1
下行(网关→单片机): dev/mcu01/cmd       # 控制命令, QoS 1
网关订阅: dev/mcu01/report(或 dev/+/report)
Broker: 板上 mosquitto 127.0.0.1:1883
```

**通用信封**(仅单片机↔网关的 MQTT;Web/Qt↔网关走 HTTP API,不用信封):
```json
{ "type": "sensor", "dev": "mcu01", "ts": "2026-08-07 10:00:00", "body": { ... } }
```
| 字段 | 说明 |
|---|---|
| type | sensor/status/cmd/ack/ai/query |
| dev | 固定 "mcu01" |
| ts | **字符串** "YYYY-MM-DD HH:MM:SS"(⚠️ 与老师 Unix 秒格式不同,网关解析要兼容两种) |
| body | payload(实际数据) |

**传感器上报(type=sensor)**:`body.data.temp/humi/light/ir`(4 个字段,单片机 cJSON 组包,2s 周期)

**状态回执(type=status)**:`body.items: [{name, state, value}]`

**命令下行(type=cmd,dev/mcu01/cmd,body 用老师字段)**:
`led_on(0/1) led_br(0-100) motor_on(0/1) motor_sp(0-100) motor_dir(0/1) buzzer(0/1)`(6 个,v1.0 的 fan/servo/beep 已废弃,舵机移除)

**HTTP REST API 全量表**(Web/Qt↔网关,老师 plan.md 第四章逐字规范,一个端点不多一个不少):

*系统接口:*

| 方法 | 路径 | 描述 | 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/api/health` | 健康检查 | - | `{"status":"ok"}` |
| GET | `/api/version` | 版本查询 | - | `{"version":"x.y.z"}` |

*设备接口:*

| 方法 | 路径 | 描述 | 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/api/devices` | 设备列表 | - | `[{DeviceJSON}, ...]` |
| GET | `/api/devices/:id` | 设备详情 | - | `{DeviceJSON}` 或 404 |
| POST | `/api/actuators/:id/set` | 下发命令 | `{"value": 1}` | `{"ok":true/false}` |

*控制接口:*

| 方法 | 路径 | 描述 | 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/api/status` | 设备状态聚合 | - | `{"temp":"25.5","humi":"60",...}` |
| POST | `/api/control` | 下发控制指令 | `{"type":"control","body":{...}}` | `{"status":"ok"}` |

*规则接口:*

| 方法 | 路径 | 描述 | 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/api/rules` | 规则列表 | - | `[{RuleJSON}, ...]` |
| POST | `/api/rules/reload` | 重载规则 | - | `{"ok":true}` |
| POST | `/api/rules/:id/enable` | 启用规则 | - | `{"ok":true/false}` |
| POST | `/api/rules/:id/disable` | 禁用规则 | - | `{"ok":true/false}` |

*摄像头接口(可选):*

| 方法 | 路径 | 描述 | 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/api/camera/status` | 查询状态 | - | `{"running":true,"url":"...","recording":false}` |
| POST | `/api/camera/start` | 启动推流 | - | `{"ok":true,"message":"..."}` |
| POST | `/api/camera/stop` | 停止推流 | - | `{"ok":true,"message":"..."}` |
| POST | `/api/camera/snapshot` | 抓拍照片 | - | `{"ok":true,"path":"...","filename":"snapshot_xxx.jpg"}` |
| POST | `/api/camera/record/start` | 开始录制 | - | `{"ok":true,"path":"...","filename":"record_xxx.mp4"}` |
| POST | `/api/camera/record/stop` | 停止录制 | - | `{"ok":true,"message":"..."}` |

**网关 /api/status 聚合兼容**:last_payload 支持信封 `$.data.value` 和扁平 `$.value` 两种(教程 5.2.3 明确)

#### Web/Qt → 网关 完整实际请求格式(Qt 同事照抄)

**⚠️ 板上端口是 8081**(8080 被 mjpg-streamer 占用);本机测试才 8080。

**① POST /api/control(按钮下发控制,核心)**

完整 HTTP 请求:
```
POST /api/control HTTP/1.1
Host: 192.168.5.70:8081
Content-Type: application/json
Content-Length: 97

{"type":"control","body":{"led_on":1,"led_br":80,"motor_on":0,"motor_sp":0,"motor_dir":0,"buzzer":0}}
```
网关处理:解析 JSON → 组 MQTT 信封 `{"type":"cmd","dev":"mcu01","ts":"...","body":{...}}` → 发布 `dev/mcu01/cmd` → 返回:
```
HTTP/1.1 200 OK
Content-Type: application/json

{"status":"ok"}
```
curl 实测命令:
```bash
curl -X POST http://192.168.5.70:8081/api/control \
  -H "Content-Type: application/json" \
  -d '{"type":"control","body":{"led_on":1,"led_br":80,"motor_on":0,"motor_sp":0,"motor_dir":0,"buzzer":0}}'
```
板上验证(无 curl,用 wget):
```bash
wget -q -O- --post-data='{"type":"control","body":{"led_on":1,"led_br":80}}' \
  --header="Content-Type: application/json" \
  http://127.0.0.1:8081/api/control
```
Qt 写法(QNetworkAccessManager):POST 到 `http://192.168.5.70:8081/api/control`,`QJsonObject body{ "type":"control", "body":{...6字段} }` → `mgr->post(req, QJsonDocument(body).toJson())` → 处理返回 `{"status":"ok"}`

**② GET /api/status(轮询看实时数据)**
```
GET http://192.168.5.70:8081/api/status
→ 响应: {"temp":"25.5","humi":"60.0","light":"500","ir":"2500","led_on":1,"led_br":80}
```
curl:`curl http://192.168.5.70:8081/api/status`;Qt:`mgr->get(QNetworkRequest(QUrl(...)))`

**③ 其他 API 同格式**:`/api/health` `/api/version` `/api/devices` `/api/actuators/:id/set` `/api/rules*` `/api/camera/*`(清单见上表)

**④ WebSocket(推送,Qt 用 QWebSocket)**:连接 `ws://192.168.5.70:8081/ws`;可替代轮询(数据一到就推)

| 方向 | 类型字段 | 描述 |
|---|---|---|
| Client → Server | (无 type) | 模拟 MQTT 发布:`{"topic":"...","payload":"..."}` |
| Server → Client | `mqtt_msg` | MQTT 消息广播:`{"type":"mqtt_msg","topic":"...","payload":"..."}` |
| Server → Client | `mqtt_pub_ack` | 发布确认:`{"type":"mqtt_pub_ack","ok":true}` |
| Server → Client | `error` | 错误信息:`{"type":"error","error":"missing_topic"}` |

### 🎓 老师规范研读:哪些配置需要动态修改(2026-08-12)

> 来源:老师 `project-plan.md`(1105 行)逐条核对。老师设计哲学 = **"配置"改文件重启,"操作"走 API 运行中执行**。除规则引擎外,没有其他必须动态改的配置。

**✅ 必须动态(老师做了专门 API)**:

| 配置 | API | 说明 |
|---|---|---|
| 规则引擎 | `GET /api/rules`、`POST /api/rules/reload`、`POST /api/rules/:id/enable`、`POST /api/rules/:id/disable` | **唯一**老师明确做成运行时可改的配置(占 20% 分) |

**✅ 运行中操作(不是配置修改,是 API 调用)**:

| 项 | API | 说明 |
|---|---|---|
| 执行器 | `POST /api/actuators/:id/set` | 运行中下发命令 |
| 摄像头 | `POST /api/camera/*`(start/stop/snapshot/record) | 运行中启停 |

**❌ 重启生效(老师明确要"改文件重启",不用动态)**:

- 阶段一验收标准原话:"修改配置文件后重启,网关能正确读取新配置"
- 日志 level:`--log-level` 启动参数(老师方案,非动态)
- 网络端口 / MQTT 地址 / 设备注册:启动时读 yaml
- `set_level()` 已有但未暴露 HTTP 接口——**这是加分优化,非老师要求,别过度设计**

**结论**:阶段 4 做规则引擎 4 个 API 即可满足老师"动态"要求;不要给日志/配置乱加动态接口(偏离老师验收)。

### 🏗️ 技术底座:单线程事件循环下的性能与线程安全约定(2026-08-11 定)

> 来源:与用户讨论"payload 是啥 / 上报后处理链 / 轮询线程安全 / 高密度请求 / 视频功能"后的架构决策。阶段 2/3/4 的代码都按此约定写。

#### 核心认知

- **mongoose 是单线程事件循环**(已读 mongoose.c 确认):HTTP 回调、MQTT 回调、定时器回调全部在**同一线程顺序执行**,任意时刻只有一个回调在跑 → 天然无"两个回调并发访问共享数据"的问题
- **HTTP 无法广播**(一问一答);"推送"靠 WebSocket(`/ws`,教程 5.2.7)。广播的实现:`MQTT收到 → 内存缓存 → ws 推送`;简单版靠 HTTP 轮询取缓存(数据先落缓存,谁问给谁)
- **铁律:事件循环线程只做微秒级的事**;慢事(磁盘/视频/大计算)全部扔出事件循环

#### 上报后网关处理链(阶段 2 实现顺序)

```
MQTT 回调 MG_EV_MQTT_MSG(on_message 挂载点)
  ① 解析信封 type/dev/ts/body(注意 ts 是字符串,兼容老师 Unix 秒两种)
  ② 提取 body.data.temp/humi/light/ir
  ③ 更新内存缓存 last_payload + last_seen(在线状态)
  ④ 触发规则引擎 OnSensorValue(阶段7,先留空接口)
  ⑤ 待写队列(微秒)← sqlite 写线程取走
  ⑥ 广播:ws 推送(阶段3)/ 或只靠轮询读缓存(阶段2)
```

#### 线程安全约定(写代码必须遵守)

| 场景 | 结论 |
|---|---|
| 事件循环线程内(HTTP回调调 publish、on_message 存缓存) | ✅ 安全,单线程顺序执行 |
| **将来加独立线程**(sqlite 写线程/worker 线程池) | ⚠️ 唯一风险源:跨线程共享数据必须加锁;**跨线程直接调 mg_\* API 禁止** |
| 轮询 /api/status 读缓存 | ✅ 安全(HTTP 回调在事件循环线程,读写同一线程) |

**决策:阶段 2 先单线程直写 sqlite(1 台设备 2s 一条,数据量小,演示够用);稳了再上"队列+写线程"(教程 4.3.5 的雏形)。**

#### sqlite 落库方案(阶段 2 可选升级)

```
MQTT回调(快):解析→更新内存→push 待写队列(微秒,不碰磁盘)
   ↓ 队列(互斥锁保护,唯一跨线程共享)
sqlite 写线程(慢):每 N 秒批量取出→事务 INSERT 多条(ms 级)
```

#### 视频功能架构(阶段 4)——网关只调度,不搬运

- **视频流**:mjpg-streamer 独立进程(8080 端口),Web **直接连 8080 看流**,网关不碰
- **拍照**:网关调 mjpg-streamer `?action=snapshot`,返回照片 URL 给前端
- **录像**:网关 spawn ffmpeg 独立进程,ffmpeg 自己录到 /data/video/
- **铁律:视频处理(ffmpeg/转码)严禁进事件循环**,全在独立进程
- 网关对视频的角色 = 调度员(返回 URL/发启动停止命令),不是搬运工

#### 高密度请求应对(现阶段结论)

- 纯读内存缓存返回 JSON:每秒几百次轮询无压力(mongoose 扛上千连接)
- **轮询只读内存缓存,永不查磁盘** → 高密度也不卡
- 回调里禁止:sleep、死循环、ffmpeg 调用
- 量级预估:1 台单片机 2s 一条 + 1-2 个前端轮询 1s → 远达不到卡顿量级,现阶段不用焦虑

#### mongoose 回调传参约定:用 fn_data,不用全局变量(2026-08-12 定)

**约定:所有 mongoose 回调(HTTP/MQTT/定时器)需要用户数据时,一律用 `fn_data`,禁止全局变量桥接。**

**原理**(已查 mongoose.c 源码验证):
- 每个 `mg_connection` 自带 `void *fn_data` 字段
- `mg_http_listen(mgr, url, fn, fn_data)` 第 4 参传入 → 存到监听连接 `c->fn_data`
- accept 新连接时**自动继承**:`c->fn_data = lsn->fn_data`(mongoose.c:5290)
- 回调里 `connect->fn_data` 直接取回

**标准写法**:
```cpp
// 1. 定义上下文结构体
struct HttpContext {
  gateway::Config config;        // 值拷贝,安全
  gateway::MqttClient *mqtt;     // 指针(mqtt 是 static 常驻)
};

// 2. main 里创建并传入
HttpContext ctx;
ctx.config = config;
ctx.mqtt = &g_mqtt;
mg_http_listen(&mgr, listen_addr, request_handler, &ctx);

// 3. 回调里取出
HttpContext *ctx = static_cast<HttpContext *>(connect->fn_data);
std::string env = control.build_control_envelope(body, ctx->config.device_id);
```

**生命周期铁律**:传入的指针(如 `&ctx`)必须存活 ≥ mgr 事件循环(放 main 栈上、for 循环之前创建;或 static/堆常驻)。程序退出后才销毁 → 安全。

**为什么不用全局变量**:
- 全局变量污染命名空间、单实例限制(多开/测试互相打架)、生命周期手动管理易漏
- `fn_data` 跟着连接走,语义清晰(请求上下文),mongoose 官方标准模式
- 项目内已有两处范例:HTTP `request_handler` 用 `HttpContext`、MQTT `event_handler` 用 `c->fn_data` 拿 `this`

### 🔄 状态同步方案:Web 控制 → Qt/Web 端刷新(2026-08-12 定,协作约定)

> 场景:Web 端控制外设 → 单片机执行 → 状态变化 → Qt 端如何同步?
> 权威来源:**单片机 status 回执**(不是网关猜的)。链路:Web --POST /api/control--> 网关 --MQTT--> 单片机执行 --> status回执 --> 网关缓存 --> 前端刷新

**当前方案:HTTP 轮询(先行,推荐)**:
- Qt/Web 端每 1 秒 `GET /api/status` → 拿到最新缓存 → 刷新界面(QTimer + QNetworkAccessManager)
- ✅ 网关零新代码(/api/status 已返回最新缓存);✅ 简单可靠,1 秒延迟对演示足够
- ❌ 非实时;轮询频率别太高(1s 合理,500ms 起就有点浪费)

**后续升级:WebSocket 推送(阶段七做)**:
- 网关加 `ws://192.168.5.70:8081/ws`,收到 MQTT 消息 → 更新缓存 → **主动推送**给所有 ws 客户端
- 推送消息格式(协议定稿):`{"type":"mqtt_msg","topic":"...","payload":"..."}`
- Qt 用 `QWebSocket` 接收;界面逻辑不变,只换数据来源(轮询→推送)
- **分工约定**:Web/Qt 同事先按轮询写,网关 ws 做好后再切换,两者读的都是 /api/status 同源数据

**⚠️ 前提铁律:status 回执必须解析**:
- /api/status 的执行器字段(led_on/motor_on/buzzer 等)来自单片机 status 回执(items 数组)
- **status 回执解析是同步链的命门**——不解析则执行器状态永远是 0,前端轮询/推送都拿不到真状态
- 当前状态:device.cpp 的 status 分支还是空的,🔴 **必须实现 items 数组遍历**(2026-08-12 待办)

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
16. **mongoose 回调传数据用 `fn_data`,别用全局变量**:`mg_http_listen(mgr, url, fn, &ctx)` 第 4 参传入,accept 新连接自动继承(`c->fn_data = lsn->fn_data`,mongoose.c:5290),回调里 `connect->fn_data` 取回。传的指针必须存活 ≥ mgr 事件循环
17. **`/api/control` 请求体键名 = `payload`**(前端 index.html 定稿 + 老师 plan.md 原文;曾临时改 body 已改回 2026-08-12);`control.cpp` 用常量 `kControlPayloadPath = "$.payload"`,将来要改只动一处。**前端控制请求已实测 200**
18. **`MG_EV_MQTT_OPEN` 的 `ev_data` 实际是 `uint8_t*`(CONNACK ack 码)**:mongoose.h 注释写 `int *connack_status_code` 是错的,以实现为准;`*(uint8_t*)ev_data == 0` 才成功
19. **mongoose 不自动发 MQTT 心跳(PINGREQ)**:`mg_mqtt_ping()` 须应用自己定时调用。网关已加 30s 心跳定时器(2026-08-13);否则 broker 在 1.5×keepalive(90s)无消息强制断开,空闲期每 ~95s 断连重连循环
20. **clangd 只在源文件父目录链找 `compile_commands.json`,不进 `build/` 子目录**。项目已用 CMake `copy_compile_commands` 目标把数据库复制到根目录(宿主构建时自动,已 gitignore);新装机器上先跑一次 `cmake --build build` 根目录才会出现
21. **系统 clangd 10(ubuntu20.04)完全不读 `.clangd` 配置文件**;`.clangd` 项目配置需 clangd 12+。老版本靠根目录 compile_commands.json 自动发现,新版本靠 `.clangd` 兜底标志,两种都可用

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
