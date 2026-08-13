# iot_gateway — RK3568 边缘智能网关

> 基于 RK3568 的边缘智能网关控制系统（大作业）。四端协作：网关（C++）、单片机（STM32）、Web 前端、Qt 上位机。
> 网关职责：对接单片机遥测数据、控制执行器、规则引擎自动联动、摄像头推流、数据持久化，并提供完整的 HTTP/WebSocket/MQTT 接口。

## 功能特性（全部完成）

| 模块 | 功能 | 说明 |
|---|---|---|
| 设备管理 | 设备注册表 + 状态缓存 | 6 外设（4 传感器 + 3 执行器）静态登记 |
| MQTT 通信 | 收发 + 断线重连 + 心跳 | mongoose 内置 MQTT，30s PINGREQ 防空闲断连 |
| 规则引擎 | 条件触发自动联动 | 温度/光照阈值触发蜂鸣器/LED，边沿触发防刷屏 |
| 摄像头 | 推流/抓拍/录像 | 方案 C：mjpg-streamer 全链路 MJPEG，零转码 |
| 数据持久化 | SQLite 遥测历史 | 队列 + 写线程，规则启停状态落库 |
| 双通道 | MQTT ↔ ZigBee 运行时切换 | DL-30 无线串口透传，前端切换按钮 |
| 实时推送 | WebSocket | MQTT 消息实时推送到前端日志 |
| 前端 | 控制台页面 | 控制/状态轮询/视频/日志/通道切换 |

## 技术栈

- **语言**：C++14（C89 的 mongoose/sqlite3 混合编译）
- **构建**：CMake ≥ 3.16
- **第三方**：mongoose 7.20（HTTP/MQTT/WS）、yaml-cpp（配置）、sqlite3（存储）——mongoose 和 sqlite3 已 vendor 进 `third_party/`，yaml-cpp 需系统安装

## 目录结构

| 目录 | 内容 | 负责人 |
|---|---|---|
| `src/` | 网关主程序（C++/CMake，模块化） | 网关 |
| `src/gateway/` | 主程序入口 + 路由 |
| `src/core/common/` | 配置/日志/MQTT/JSON 工具 |
| `src/core/device/` | 设备状态 + 注册表 |
| `src/core/control/` | 控制信封组包 |
| `src/core/rules/` | 规则引擎 |
| `src/core/camera/` | 摄像头管理 |
| `src/core/storage/` | SQLite 持久化 |
| `src/core/channel/` | 通道管理（MQTT/ZigBee） |
| `mcu/` | 单片机固件（STM32/FreeRTOS） | 单片机 |
| `web/` | Web 前端（HTML/JS） | Web |
| `qt/` | Qt 上位机（Qt5） | Qt |
| `config/` | 配置文件（gateway.yaml/devices/rules） | 全部 |
| `third_party/` | 第三方库（mongoose/sqlite3） | 网关 |
| `docs/` | 项目文档 | 全部 |

## 设备清单

设备号固定 `mcu01`，共 6 外设：

| id | 类型 | 说明 | 字段 |
|---|---|---|---|
| `temp_1` | sensor | 温度 | `temp` |
| `humi_1` | sensor | 湿度 | `humi` |
| `light_1` | sensor | 光照 | `light` |
| `ir_1` | sensor | 红外 | `ir` |
| `led_1` | actuator | LED 灯 | `led_on` / `led_br` |
| `motor_1` | actuator | 电机 | `motor_on` / `motor_sp` / `motor_dir` |
| `buzzer_1` | actuator | 蜂鸣器 | `buzzer` |

## 快速开始

### 1. 装依赖（一次性）

```bash
sudo apt install cmake g++ libyaml-cpp-dev
```

### 2. 编译 + 运行

```bash
cmake -B build && cmake --build build   # 编译
./build/gateway                          # 启动(端口 8081,读同目录 config/)
```

浏览器打开 `http://<网关IP>:8081/` 即可看到控制台页面。

### 3. 交叉编译（ARM 板上）

需 Linaro 工具链 `/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu`（yaml-cpp 已装进 sysroot，见 `docs/HANDOFF.md` 5.2 节）：

```bash
./build-arm.sh            # 编译 ARM 版
./build-arm.sh --deploy   # 编译 + 部署到板子 + 自动验证
```

## API 接口

完整接口文档见 **`docs/API接口对接文档.md`**（含所有请求/响应示例、错误码、单设备操控说明）。

接口概览：

| 分类 | 接口 |
|---|---|
| 系统 | `GET /api/health`、`GET /api/version` |
| 设备 | `GET /api/devices`、`GET /api/devices/:id`、`POST /api/actuators/:id/set`、`GET /api/status` |
| 控制 | `POST /api/control` |
| 规则 | `GET /api/rules`、`POST /api/rules/reload`、`/enable`、`/disable` |
| 摄像头 | `/api/camera/start_stream`、`stop_stream`、`start_record`、`stop_record`、`snapshot`、`status` |
| 通道 | `GET /api/channel`、`POST /api/channel/switch` |
| 历史 | `GET /api/history?limit=N` |
| 实时 | `ws://<IP>:8081/ws`（WebSocket） |

## 双通道通信

网关 ↔ 单片机支持 **MQTT**（WiFi/以太网）和 **ZigBee**（DL-30 无线串口）双通道运行时切换：

- 前端有"通道"切换按钮，或调 `POST /api/channel/switch {"transport":"zigbee"}`
- 切到 ZigBee 后，所有控制命令自动改走串口（`/dev/ttyS4` @ 115200），无需改代码
- ZigBee 模块未插时切换返回 503，MQTT 通道照常工作

## 运行前置（配置文件）

启动时从**相对路径**读取，需保证同目录结构：

```
config/gateway.yaml       # 主配置（端口/MQTT/设备号/摄像头/zigbee）
config/devices/*.yaml     # 设备注册表（sensors.yaml + actuators.yaml）
config/rules/rules.yaml   # 规则引擎（演示 4 条规则）
web/index.html            # 前端页面
```

## 协作约定

- 各负责人只在对应目录内提交代码，避免跨目录改动造成冲突。
- 配置文件（config/）由全部人共同维护，修改前先在群里同步。
- 文档（docs/）统一归档到该目录，不要在根目录散落 markdown。
- 空目录使用 `.gitkeep` 占位，保证目录结构被 git 跟踪。
