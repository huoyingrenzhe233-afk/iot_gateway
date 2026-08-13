# 网关 API 对接文档(单片机 / Qt 同事对接用)

> 最后更新:2026-08-13
> 适用范围:RK3568 边缘智能网关全部 HTTP / WebSocket / MQTT 接口
> 对接对象:单片机同事(MQTT 协议)、Qt 同事(HTTP 轮询/控制)、Web 前端

---

## 1. 基础信息

| 项 | 值 | 说明 |
|---|---|---|
| 网关 IP | `192.168.5.70` | 板上固定 IP |
| 网关 HTTP 端口 | **8081** | 所有 `/api/*` 走这里 |
| 摄像头流端口 | **8080** | mjpg-streamer 的 MJPEG 流(浏览器 `<img>` 直连) |
| 前端页面 | `http://192.168.5.70:8081/` | 网关伺服的控制台页面 |
| 返回格式 | JSON | 所有 `/api/*` 均返回 `Content-Type: application/json` |
| MQTT Broker | `192.168.5.70:1883` | mosquitto,匿名免密 |

**注意**:8080 被摄像头占用,HTTP 接口统一走 8081。本机(开发机)测试时用 8080,板上永远 8081。

---

## 2. 设备清单(重要!所有 id 以此为准)

本项目**只有一台单片机**(设备号固定 `mcu01`),共 **6 个外设**:4 传感器 + 3 执行器(温湿度一体出 2 个测量字段,登记为 2 条)。

### 传感器(4 条登记,对应 4 个上报字段)

| id | 类型 | 说明 | 上报字段 |
|---|---|---|---|
| `temp_1` | sensor | 温度(温湿度传感器) | `body.data.temp` |
| `humi_1` | sensor | 湿度(温湿度传感器) | `body.data.humi` |
| `light_1` | sensor | 光照(光敏传感器) | `body.data.light` |
| `ir_1` | sensor | 红外(红外传感器) | `body.data.ir` |

### 执行器(3 条登记,对应 6 个控制字段)

| id | 类型 | 说明 | 可控制字段 |
|---|---|---|---|
| `led_1` | actuator | LED 灯 | `led_on`(开关)、`led_br`(亮度 0-100) |
| `motor_1` | actuator | 电机 | `motor_on`(开关)、`motor_sp`(速度 0-100)、`motor_dir`(方向 0/1) |
| `buzzer_1` | actuator | 蜂鸣器 | `buzzer`(开关) |

⚠️ **id 是"业务标识"(`temp_1`/`led_1`),字段是"协议字段"(`temp`/`led_on`),两套名字,别混**。HTTP 接口用 id,MQTT 报文用字段。

---

## 3. 系统接口

### 3.1 `GET /api/health` — 健康检查

```
请求: GET http://192.168.5.70:8081/api/health
响应 200: {"status":"ok"}
```

### 3.2 `GET /api/version` — 版本查询

```
请求: GET http://192.168.5.70:8081/api/version
响应 200: {"version":"1.0.0"}
```

---

## 4. 设备接口

### 4.1 `GET /api/devices` — 设备列表

返回全部登记设备(传感器 + 执行器)。

```
请求: GET http://192.168.5.70:8081/api/devices
响应 200:
[
  {"id":"temp_1","kind":"sensor","protocol":"mqtt","description":"温度(温湿度传感器)"},
  {"id":"humi_1","kind":"sensor","protocol":"mqtt","description":"湿度(温湿度传感器)"},
  {"id":"light_1","kind":"sensor","protocol":"mqtt","description":"光照(光敏传感器)"},
  {"id":"ir_1","kind":"sensor","protocol":"mqtt","description":"红外(红外传感器)"},
  {"id":"led_1","kind":"actuator","protocol":"mqtt","description":"LED灯(led_on/led_br)"},
  {"id":"motor_1","kind":"actuator","protocol":"mqtt","description":"电机(motor_on/motor_sp/motor_dir)"},
  {"id":"buzzer_1","kind":"actuator","protocol":"mqtt","description":"蜂鸣器(buzzer)"}
]
```

字段说明:`kind` = `sensor`(传感器)/`actuator`(执行器);`protocol` = 通信协议(当前恒 `mqtt`)。

### 4.2 `GET /api/devices/{id}` — 单设备详情 ⭐

**这就是"单设备查询"接口**——查某一个外设的详细信息 + 在线状态。

```
请求: GET http://192.168.5.70:8081/api/devices/temp_1
响应 200:
{"id":"temp_1","kind":"sensor","protocol":"mqtt","description":"温度(温湿度传感器)","online":true,"last_seen":"2026-08-13 14:38:41"}

请求: GET http://192.168.5.70:8081/api/devices/nope
响应 404: {"error":"device_not_found"}
```

字段说明:
| 字段 | 说明 |
|---|---|
| `online` | 是否在线(收到过该设备上报 = `true`;单 mcu01 场景所有外设共用) |
| `last_seen` | 最后一次上报的时间戳(空 = 从未上报) |

### 4.3 `POST /api/actuators/{id}/set` — 单执行器控制 ⭐

**这就是"单设备操控"接口**——单独控制某一个执行器(不用发全部 6 个字段)。

```
请求: POST http://192.168.5.70:8081/api/actuators/buzzer_1/set
请求体: {"value": 1}
响应 200: {"ok":true}
```

| id | 控制的字段 | 示例 |
|---|---|---|
| `led_1` | `led_on`(0 关 / 1 开) | `{"value":1}` 开灯 |
| `motor_1` | `motor_on`(0 停 / 1 转) | `{"value":0}` 停电机 |
| `buzzer_1` | `buzzer`(0 停 / 1 响) | `{"value":1}` 蜂鸣 |

**错误响应**:
```
id 不存在 → 404 {"error":"actuator_not_found"}
请求体缺 value → 400 {"error":"missing value"}
当前通道未就绪(MQTT 掉线 / zigbee 未插) → 503 {"ok":false}
```

⚠️ **注意**:`led_1` 只能开关(`led_on`),不能通过此接口调亮度(`led_br`);调亮度要用 4.4 的 `/api/status` 全字段接口 `/api/control`。

### 4.4 `GET /api/status` — 设备状态聚合(轮询核心接口)

返回 10 个字段:4 传感器测量值(字符串)+ 6 执行器状态(数值)。**前端/Qt 每 1 秒轮询这个接口刷新界面**。

```
请求: GET http://192.168.5.70:8081/api/status
响应 200:
{"temp":"25.5","humi":"60.1","light":"320","ir":"2500","led_on":1,"led_br":80,"motor_on":0,"motor_sp":0,"motor_dir":0,"buzzer":0}
```

字段说明:
| 字段 | 类型 | 含义 |
|---|---|---|
| `temp` `humi` `light` `ir` | string | 传感器测量值(协议定为字符串,`%g` 格式,`25.0` 会显示 `25`) |
| `led_on` `buzzer` | int | 0/1 开关 |
| `led_br` `motor_sp` | int | 0-100 PWM |
| `motor_on` | int | 0/1 |
| `motor_dir` | int | 0 正转 / 1 反转 |

未收到过上报时,传感器字段为空字符串 `""`。

---

## 5. 控制接口

### 5.1 `POST /api/control` — 全字段控制(老师规范的主控制接口)

一次下发全部 6 个执行器字段(适合"控制面板保存"场景)。

```
请求: POST http://192.168.5.70:8081/api/control
请求头: Content-Type: application/json
请求体:
{
  "type": "control",
  "payload": {
    "led_on": 1, "led_br": 80,
    "motor_on": 0, "motor_sp": 50, "motor_dir": 0,
    "buzzer": 0
  }
}
响应 200: {"status":"ok"}
```

**错误响应**:
```
请求体缺 payload → 400 {"status":"error","message":"missing payload"}
当前通道未就绪(MQTT 掉线 / zigbee 未插) → 503 {"status":"error","message":"channel not ready"}
```

⚠️ **键名是 `payload`**(不是 `body`),这是老师 plan.md + 前端定稿的字段。
`payload` 里的字段可以**部分下发**(缺哪个就保持现状,不会被重置)。

Qt 写法(QNetworkAccessManager):
```cpp
QJsonObject body;
body["type"] = "control";
QJsonObject payload;
payload["led_on"] = 1; payload["led_br"] = 80;
// ... 其余字段同理
body["payload"] = payload;
// POST http://192.168.5.70:8081/api/control
// 发送 QJsonDocument(body).toJson()
```

---

## 6. 规则引擎接口

### 6.1 `GET /api/rules` — 规则列表

```
请求: GET http://192.168.5.70:8081/api/rules
响应 200:
[
  {"id":"temp_alarm","name":"高温报警","enabled":true,
   "when":{"sensor":"temp_1","op":">","value":30},
   "then":{"actuator":"buzzer_1","field":"buzzer","value":1}},
  ...
]
```

字段:`when` = 触发条件(传感器 id + 比较符 + 阈值);`then` = 动作(执行器 id + 字段 + 值);`enabled` = 是否启用。

### 6.2 `POST /api/rules/reload` — 重载规则(运行时改配置)

```
请求: POST http://192.168.5.70:8081/api/rules/reload
响应 200: {"ok":true}
失败: {"ok":false,"message":"reload failed"}
```

改了 `config/rules/rules.yaml` 后调这个,不用重启网关。

### 6.3 `POST /api/rules/{id}/enable` / `disable` — 启停规则

```
请求: POST http://192.168.5.70:8081/api/rules/temp_alarm/disable
响应 200: {"ok":true}

规则不存在 → 404 {"ok":false,"message":"rule_not_found"}
```

---

## 7. 摄像头接口

> 方案 C:全链路 MJPEG。`start_stream` 后网关会 fork `mjpg_streamer`,前端用 `<img>` 直连 8080 看流。以下接口 **GET / POST 都接受**。

| 接口 | 方法 | 说明 | 响应 |
|---|---|---|---|
| `/api/camera/start_stream` | GET/POST | 启动推流 | 200 `{"ok":true}` / 500 |
| `/api/camera/stop_stream` | GET/POST | 停止推流 | 200 `{"ok":true}` |
| `/api/camera/start_record` | GET/POST | 开始录像 | 200 `{"ok":true}` |
| `/api/camera/stop_record` | GET/POST | 停止录像 | 200 `{"ok":true}` |
| `/api/camera/snapshot` | GET/POST | 抓拍一帧 | 200 `{"ok":true,"filename":"snapshot_xxx.jpg"}` |
| `/api/camera/status` | GET/POST | 状态查询 | `{"running":false,"recording":false}` |

**视频流地址**(推流启动后,前端 `<img>` 直接显示):
```
http://192.168.5.70:8080/?action=stream
```

**录像顺序**:先 `start_stream`(推流),再 `start_record`(录像);录像保存到网关 `records/` 目录。

---

## 8. 通道切换接口(MQTT ↔ ZigBee)

本项目网关↔单片机支持**双通道切换**:MQTT(WiFi/以太网)和 ZigBee(DL-30 无线串口)。

### 8.1 `GET /api/channel` — 当前通道

```
请求: GET http://192.168.5.70:8081/api/channel
响应 200: {"transport":"mqtt"}  或  {"transport":"zigbee"}
```

### 8.2 `POST /api/channel/switch` — 切换通道

```
请求: POST http://192.168.5.70:8081/api/channel/switch
请求体: {"transport":"zigbee"}
响应 200: {"ok":true,"transport":"zigbee"}
```

| 情况 | 响应 |
|---|---|
| 成功 | 200 `{"ok":true,"transport":"..."}` |
| 目标通道未就绪(如 ZigBee 模块没插) | **503** `{"ok":false,"message":"channel not ready"}` |
| 缺 `transport` 字段 | 400 `{"ok":false,"message":"missing transport"}` |
| 非法值(非 mqtt/zigbee) | 400 `{"ok":false,"message":"invalid transport"}` |

**对 Qt 同事**:界面上做"切换按钮",点击后 POST 这个接口;切到 zigbee 后,所有控制命令(`/api/control`、`/api/actuators/:id/set`、规则动作)自动改走 ZigBee 串口,不用改别的代码。

---

## 9. 历史数据接口

### 9.1 `GET /api/history?limit=N` — 遥测历史(前端曲线)

```
请求: GET http://192.168.5.70:8081/api/history?limit=100
响应 200(时间升序,最多 N 条):
[{"ts":"t1","temp":25,"humi":60,"light":320,"ir":2500},
 {"ts":"t2","temp":26},          ← 单独上报的字段,缺的字段省略键
 ...]
```

| 参数 | 说明 |
|---|---|
| `limit` | 返回条数,缺省 100,上限 1000 |

数据来源:单片机每次上报都落 SQLite 库(遥测历史,无限存)。

---

## 10. WebSocket 实时推送

用于"实时日志"——单片机每发一条上报,网关立刻推给所有已连接的 WS 客户端。

```
连接: ws://192.168.5.70:8081/ws
```

### 服务端 → 客户端(收到 MQTT 上报时广播)

```json
{"type":"mqtt_msg","topic":"dev/mcu01/report","payload":"{\"type\":\"sensor\",...}"}
```

### 客户端 → 服务端(模拟 MQTT 发布)

```json
{"topic":"dev/mcu01/cmd","payload":"{\"led_on\":1}"}
```

服务端回:
```json
{"type":"mqtt_pub_ack","ok":true}
```

| 服务端消息类型 | 含义 |
|---|---|
| `mqtt_msg` | MQTT 消息广播(实时日志) |
| `mqtt_pub_ack` | 发布确认 |
| `error` | `{"type":"error","error":"missing_topic"/"mqtt_not_connected"}` |

Qt 用 `QWebSocket` 连接 `ws://192.168.5.70:8081/ws`。

---

## 11. MQTT 协议(单片机同事专用)

### 11.1 Topic

```
上行(单片机 → 网关):  dev/mcu01/report   # 传感器数据/状态回执, QoS 1
下行(网关 → 单片机):  dev/mcu01/cmd      # 控制命令, QoS 1
Broker: 192.168.5.70:1883
```

### 11.2 通用信封(所有 MQTT 报文)

```json
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:00","body":{...}}
```

| 字段 | 说明 |
|---|---|
| `type` | 报文类型:`sensor`(上报)/`status`(回执)/`cmd`(命令) |
| `dev` | 固定 `"mcu01"` |
| `ts` | 时间戳字符串 `YYYY-MM-DD HH:MM:SS` |
| `body` | 实际数据 |

### 11.3 传感器上报(type=sensor)

单片机每 2 秒上报一次,4 个字段**可以一起发也可以单独发**:

```json
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:00",
 "body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":2500}}}

// 单独发温度(其余字段网关保持不变):
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:02",
 "body":{"data":{"temp":26.0}}}
```

### 11.4 状态回执(type=status)—— 执行完命令后回执

单片机执行完控制命令后,回一条 status 回执,网关据此校准执行器真实状态:

```json
{"type":"status","dev":"mcu01","ts":"2026-08-13 10:00:05",
 "body":{"items":[
   {"name":"led","state":"on","value":1},
   {"name":"buzzer","state":"off","value":0}
 ]}}
```

`name` 支持短名(`led`/`motor`/`buzzer`)和长名(`led_on` 等)两套。

### 11.5 控制命令(type=cmd)—— 网关发给你

```json
{"type":"cmd","dev":"mcu01","ts":"2026-08-13 10:00:00",
 "body":{"led_on":1,"led_br":80,"motor_on":0,"motor_sp":50,"motor_dir":0,"buzzer":0}}
```

命令的 6 个字段可能**只含部分**(部分下发),你收到哪个字段就执行哪个,没收到的保持原状。

---

## 12. 错误码约定汇总

| HTTP 状态码 | 场景 | 响应体 |
|---|---|---|
| 200 | 成功 | 各接口见上 |
| 400 | 请求体缺字段/非法值 | `{"error":"missing value"}` 等 |
| 404 | id/路径不存在 | `{"error":"device_not_found"}` / `{"error":"actuator_not_found"}` / `{"error":"rule_not_found"}` |
| 500 | 内部未就绪 | `{"error":"..."}` |
| 503 | 依赖未就绪(MQTT 未连/通道未就绪) | `{"ok":false}` / `{"ok":false,"message":"channel not ready"}` |

---

## 13. 单设备场景说明(重要!)

本项目是**单设备(单单片机)场景**,所有数据都围绕唯一设备 `mcu01`:

- **单设备操控** = `POST /api/actuators/{id}/set`,一次控制**一个执行器**(`led_1`/`motor_1`/`buzzer_1`),body 只要 `{"value":1}`。
- **单设备查询** = `GET /api/devices/{id}`,查**一个外设**的详情 + 在线状态。
- **全设备状态** = `GET /api/status`,一次拿 6 外设的 10 字段(轮询用)。
- **全设备控制** = `POST /api/control`,一次发 6 字段(控制面板保存用)。

**对 Qt 同事的建议**:
- 界面刷新:每 1 秒轮询 `GET /api/status`。
- 单按钮(如"开 LED"):用 `POST /api/actuators/led_1/set {"value":1}`。
- 完整设置面板(滑杆+开关):用 `POST /api/control`(6 字段一起发)。
- 实时日志:WebSocket 连 `/ws`,收到 `mqtt_msg` 就追加到日志框。

**对单片机同事的建议**:
- 每 2 秒上报 sensor(4 字段可一起可分开)。
- 收到 cmd 后执行,执行完回 status 回执(让网关/前端显示真实状态)。
- 上报/回执/命令都用同一信封 `{"type","dev":"mcu01","ts","body"}`。

---

## 14. curl 速测命令(板上,无 curl 用 wget -q -O-)

```bash
# 健康检查
wget -q -O- http://127.0.0.1:8081/api/health

# 设备列表
wget -q -O- http://127.0.0.1:8081/api/devices

# 单设备详情
wget -q -O- http://127.0.0.1:8081/api/devices/temp_1

# 单执行器控制(开蜂鸣器)
wget -q -O- --post-data='{"value":1}' http://127.0.0.1:8081/api/actuators/buzzer_1/set

# 状态轮询
wget -q -O- http://127.0.0.1:8081/api/status

# 切换 zigbee 通道
wget -q -O- --post-data='{"transport":"zigbee"}' http://127.0.0.1:8081/api/channel/switch
```
