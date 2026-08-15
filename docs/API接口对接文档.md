# 网关 API 对接文档(Qt / Web / 单片机 对接用)

> 最后更新:2026-08-14
> 适用范围:RK3568 边缘智能网关全部 HTTP / WebSocket / MQTT 接口
> 对接对象:Qt 同事、Web 前端、单片机同事

---

## 1. 30 秒上手

- **Qt / Web 同事**:先看下方 §2「按场景找接口」,找到你要做的事 → 跳到对应章节抄示例。
- **想看全部接口**:直接看 §3「接口速查总表」。
- **单片机同事**:直接跳 §14「MQTT 协议」。
- **本地快速验证**:抄 §16「速测命令」。

---

## 2. 按场景找接口(我要做什么 → 用什么)

| 我要做的事 | 用这个接口 | 详见 |
|---|---|---|
| 看实时设备状态(界面刷新) | `GET /api/status`,**每 1 秒轮询** | §7.4 |
| 开/关某一个执行器(单个按钮) | `POST /api/actuators/{id}/set` | §7.3 |
| 改亮度/速度等(控制面板) | `POST /api/control`(部分字段下发) | §8 |
| 看设备清单 / 单个设备详情 | `GET /api/devices` / `GET /api/devices/{id}` | §7.1 / §7.2 |
| 看规则 / 启停规则 / 热重载 | `GET /api/rules` / `POST /api/rules/{id}/enable\|disable` / `POST /api/rules/reload` | §9 |
| 看历史数据曲线 | `GET /api/history?limit=N` | §12 |
| 实时日志(MQTT 消息推送) | WebSocket `ws://…:8081/ws` | §13 |
| 切换 MQTT/ZigBee 通道 | `POST /api/channel/switch`;当前通道查 `GET /api/channel` | §11 |
| 摄像头:推流/拍照/录像 | `/api/camera/*` | §10 |
| 健康检查 / 版本 | `GET /api/health` / `GET /api/version` | §6 |
| (单片机)上报数据 / 收命令 | MQTT topic + 信封格式 | §14 |

---

## 3. 接口速查总表(全部端点一览)

| 类别 | 方法 | 路径 | 用途 | 详见 |
|---|---|---|---|---|
| 系统 | GET | `/api/health` | 健康检查 | §6.1 |
| 系统 | GET | `/api/version` | 版本查询 | §6.2 |
| 设备 | GET | `/api/devices` | 设备列表 | §7.1 |
| 设备 | GET | `/api/devices/{id}` | 单设备详情 | §7.2 |
| 设备 | POST | `/api/actuators/{id}/set` | 单执行器控制 | §7.3 |
| 设备 | GET | `/api/status` | 状态聚合(轮询核心) | §7.4 |
| 控制 | POST | `/api/control` | 全字段/部分字段控制 | §8.1 |
| 规则 | GET | `/api/rules` | 规则列表 | §9.1 |
| 规则 | POST | `/api/rules/reload` | 热重载规则 | §9.2 |
| 规则 | POST | `/api/rules/{id}/enable` / `/disable` | 启停规则 | §9.3 |
| 摄像头 | GET/POST | `/api/camera/start_stream`(别名 `…/start`) | 启动推流 | §10 |
| 摄像头 | GET/POST | `/api/camera/stop_stream`(别名 `…/stop`) | 停止推流 | §10 |
| 摄像头 | GET/POST | `/api/camera/start_record`(别名 `…/record/start`) | 开始录像 | §10 |
| 摄像头 | GET/POST | `/api/camera/stop_record`(别名 `…/record/stop`) | 停止录像 | §10 |
| 摄像头 | GET/POST | `/api/camera/snapshot` | 抓拍一帧 | §10 |
| 摄像头 | GET/POST | `/api/camera/status` | 推流/录像状态 | §10 |
| 摄像头 | GET | `/snapshots/{filename}` | 取回抓拍照片 | §10 |
| 通道 | GET | `/api/channel` | 当前通道 | §11.1 |
| 通道 | POST | `/api/channel/switch` | 切换通道 | §11.2 |
| 历史 | GET | `/api/history?limit=N` | 遥测历史 | §12 |
| 推送 | WS | `/ws` | WebSocket 实时推送 | §13 |

---

## 4. 基础信息

| 项 | 值 | 说明 |
|---|---|---|
| 网关 IP | `192.168.5.70`(有线,固定) | 板子目前走 WiFi:`10.137.31.9`(**IP 会变,以实际为准**) |
| 网关 HTTP 端口 | **8081** | 所有 `/api/*` 走这里 |
| 摄像头流端口 | **8080** | mjpg-streamer 的 MJPEG 流(浏览器 `<img>` 直连) |
| 前端页面 | `http://<网关IP>:8081/` | 网关伺服的控制台页面 |
| 返回格式 | JSON | 所有 `/api/*` 均返回 `Content-Type: application/json` |
| MQTT Broker | `<网关IP>:1883` | mosquitto,匿名免密 |

**注意**:8080 被摄像头占用,HTTP 接口统一走 8081。本机(开发机)测试时用 8080,板上永远 8081。

---

## 5. 设备清单(重要!所有 id 以此为准)

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

## 6. 系统接口

### 6.1 `GET /api/health` — 健康检查

```
请求: GET http://<网关IP>:8081/api/health
响应 200: {"status":"ok"}
```

### 6.2 `GET /api/version` — 版本查询

```
请求: GET http://<网关IP>:8081/api/version
响应 200: {"version":"1.0.0"}
```

---

## 7. 设备接口

### 7.1 `GET /api/devices` — 设备列表

返回全部登记设备(传感器 + 执行器)。

```
请求: GET http://<网关IP>:8081/api/devices
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

### 7.2 `GET /api/devices/{id}` — 单设备详情

查某一个外设的详细信息 + 在线状态。

```
请求: GET http://<网关IP>:8081/api/devices/temp_1
响应 200:
{"id":"temp_1","kind":"sensor","protocol":"mqtt","description":"温度(温湿度传感器)","online":true,"last_seen":"2026-08-13 14:38:41"}

请求: GET http://<网关IP>:8081/api/devices/nope
响应 404: {"error":"device_not_found"}
```

| 字段 | 说明 |
|---|---|
| `online` | 是否在线(收到过该设备上报 = `true`;单 mcu01 场景所有外设共用) |
| `last_seen` | 最后一次上报的时间戳(空 = 从未上报) |

### 7.3 `POST /api/actuators/{id}/set` — 单执行器控制

单独控制某一个执行器(不用发全部 6 个字段)。**多客户端并存时优先用这个接口**,避免全量覆盖。

```
请求: POST http://<网关IP>:8081/api/actuators/buzzer_1/set
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
id 不存在            → 404 {"error":"actuator_not_found"}
请求体缺 value       → 400 {"error":"missing value"}
value 不是数字       → 400 {"error":"invalid value"}
当前通道未就绪       → 503 {"ok":false}
```

⚠️ **`value` 必须是 JSON 数字**(如 `{"value":1}`)。传字符串 `{"value":"1"}` 会被拒绝(400),不会执行任何动作——Qt 从文本框取值时注意 `toInt()` 后再塞进 JSON。

⚠️ `led_1` 通过此接口只能开关(`led_on`);调亮度(`led_br`)要用 §8 的 `/api/control`。

💡 **LED 开关命令附带亮度**:网关下发 `led_on` 时,信封里**总是同时带 `led_br`**(当前缓存亮度,从未设置过时默认 50)——`{"led_on":1,"led_br":50}`。这样整帧解析型 MCU 固件(只收到 led_on 没 led_br 会把 PWM 置 0)也能正确亮灯。规则引擎的"暗光开灯"动作同理。

### 7.4 `GET /api/status` — 设备状态聚合(轮询核心接口)

一次返回 12 个字段:4 传感器测量值 + 6 执行器状态 + 1 个 `last_report` 上报时间 + 1 个 `transport` 通道字段。**前端/Qt 每 1 秒轮询这个接口刷新界面**。

```
请求: GET http://<网关IP>:8081/api/status
响应 200:
{"temp":"25.5","humi":"60.1","light":"320","ir":"2500",
 "led_on":1,"led_br":80,"motor_on":0,"motor_sp":0,"motor_dir":0,"buzzer":0,
 "last_report":"2026-08-15 10:00:00",
 "transport":"mqtt"}
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `temp` `humi` `light` `ir` | string | 传感器测量值(协议定为字符串,`%g` 格式,`25.0` 会显示 `25`) |
| `led_on` `buzzer` | int | 0/1 开关 |
| `led_br` `motor_sp` | int | 0-100 PWM |
| `motor_on` | int | 0/1 |
| `motor_dir` | int | 0 正转 / 1 反转 |
| `last_report` | string | 最近一次上报时间(空串 `""` = 从未收到上报);来源 `Device::last_seen_` |
| `transport` | string | 当前通信通道(`mqtt`/`zigbee`);通道切换按钮状态可直接读它 |

未收到过上报时,传感器字段为空字符串 `""`。

> 💡 **轮询值 = 网关缓存(乐观值)**:命令下发后 `/api/status` 立即反映,约 2 秒后以单片机 status 回执校准为准,期间可能出现"先变、再微调"——这是设计行为,不是 bug。

---

## 8. 控制接口

### 8.1 `POST /api/control` — 控制命令下发(核心)

适合"控制面板"场景:一次下发执行器字段,**支持部分字段**(缺哪个就保持现状,不会被重置)。

```
请求: POST http://<网关IP>:8081/api/control
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
当前通道未就绪   → 503 {"status":"error","message":"channel not ready"}
```

⚠️ **键名是 `payload`**(不是 `body`),这是老师 plan.md + 前端定稿的字段。

⚠️ **双端并存约定**:Web 和 Qt 同时操作时,**只下发"有变化"的字段**,不要各自拿 1 秒前的旧快照全量覆盖,否则会互相冲掉对方的设置(如 A 调了亮度,B 的全量请求又把旧亮度写回去)。单按钮优先用 §7.3。

Qt 写法(QNetworkAccessManager):
```cpp
QJsonObject body;
body["type"] = "control";
QJsonObject payload;
payload["led_on"] = 1; payload["led_br"] = 80;
// ... 其余字段同理
body["payload"] = payload;
// POST http://<网关IP>:8081/api/control
// 发送 QJsonDocument(body).toJson()
```

---

## 9. 规则引擎接口

### 9.1 `GET /api/rules` — 规则列表

```
请求: GET http://<网关IP>:8081/api/rules
响应 200:
[
  {"id":"temp_alarm","name":"高温报警","enabled":true,
   "when":{"sensor":"temp_1","op":">","value":30},
   "then":{"actuator":"buzzer_1","field":"buzzer","value":1}},
  ...
]
```

字段:`when` = 触发条件(传感器 id + 比较符 + 阈值);`then` = 动作(执行器 id + 字段 + 值);`enabled` = 是否启用。

### 9.2 `POST /api/rules/reload` — 重载规则

改了 `config/rules/rules.yaml` 后调这个,不用重启网关。

```
请求: POST http://<网关IP>:8081/api/rules/reload
响应 200: {"ok":true}
失败:    {"ok":false,"message":"reload failed"}
```

### 9.3 `POST /api/rules/{id}/enable` / `disable` — 启停规则

```
请求: POST http://<网关IP>:8081/api/rules/temp_alarm/disable
响应 200: {"ok":true}

规则不存在 → 404 {"ok":false,"message":"rule_not_found"}
```

> 💡 规则触发是**边沿触发**:条件从"不满足→满足"的瞬间才下发一次动作,持续满足不重复下发;恢复(如"高温解除")由配套规则负责。手动控制与规则联动并存时,状态以最后执行的命令为准。

---

## 10. 摄像头接口

> **生命周期模型(重要)**:视频流由**网关托管**——网关启动时自动拉起 mjpg_streamer 并常驻;Web/Qt 客户端**只负责"看画面"**(直接拉 8080 的 MJPEG 流),**不负责启停流**。因此任意一端打开/关闭页面都只影响自己的观看连接,不会影响另一端的画面。"停止推流"接口仅作管理员用途。以下接口 **GET / POST 都接受**。

| 接口 | 方法 | 说明 | 响应 |
|---|---|---|---|
| `/api/camera/start_stream`(别名 `/api/camera/start`) | GET/POST | 确保推流在跑(**幂等**:已在跑返回 already running);客户端打开页面时调用它兜底(如板子重启/摄像头重插后自动恢复) | 200 `{"ok":true}` / 500 |
| `/api/camera/stop_stream`(别名 `/api/camera/stop`) | GET/POST | **管理员手动关闭推流**(会中断所有客户端的画面;正常 UI 不要调用) | 200 `{"ok":true}` |
| `/api/camera/start_record`(别名 `/api/camera/record/start`) | GET/POST | 开始录像(**需先推流**,幂等) | 200 `{"ok":true}` / 500 |
| `/api/camera/stop_record`(别名 `/api/camera/record/stop`) | GET/POST | 停止录像(**只停录像,不影响画面**) | 200 `{"ok":true}` |
| `/api/camera/snapshot` | GET/POST | 抓拍一帧 | 200 `{"ok":true,"filename":"snapshot_xxx.jpg"}` |
| `/api/camera/status` | GET/POST | 状态查询 | `{"running":false,"recording":false}` |
| `GET /snapshots/{filename}` | GET | 取回抓拍照片 | 200 图片 / 404 |

> 别名(`start/stop`、`record/start|stop`)是为兼容老师 project-plan.md 的接口名,两套名字行为完全一致,用哪套都行。

**看画面**(Web/Qt 通用,不需要启停接口):
```
推流已由网关常驻 → 客户端直接拉:http://<网关IP>:8080/?action=stream
(Web 用 <img>;Qt 用 QNetworkAccessManager 或 QML Image 拉同一地址)
可选兜底:打开页面时调一次 /api/camera/start_stream(幂等),防止板子重启后流未拉起
```

**对 Qt 同事(界面按钮约定)**:界面可以放"开始视频 / 停止视频"按钮,但语义必须是**本端观看控制**:"开始视频" = 调一次 `/api/camera/start_stream`(幂等 ensure)+ 拉 8080 流显示画面;"停止视频" = **只断开本端观看连接**(Qt 侧停掉拉流请求/清空画面),**不要调用 stop_stream**。全局流的关闭只属于管理员操作(`stop_stream`),任何客户端都不该在普通 UI 里触发它。

**抓拍照片展示**:`snapshot` 返回的 `filename` 拼 `http://<网关IP>:8081/snapshots/<filename>` 即可用 `<img>` 显示。

**录像顺序**:先确保推流(常驻,一般已就绪)→ 再 `start_record`;停止录像用 `stop_record`(**不影响画面**)。录像保存到网关 `records/` 目录。

---

## 11. 通道切换接口(MQTT ↔ ZigBee)

网关↔单片机支持**双通道切换**:MQTT(WiFi/以太网)和 ZigBee(DL-30 无线串口)。

### 11.1 `GET /api/channel` — 当前通道

```
请求: GET http://<网关IP>:8081/api/channel
响应 200: {"transport":"mqtt"}  或  {"transport":"zigbee"}
```

### 11.2 `POST /api/channel/switch` — 切换通道

```
请求: POST http://<网关IP>:8081/api/channel/switch
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

**对单片机同事**:网关切换成功后,会**经旧通道**给单片机下发一条通道切换通知(见 §14.6 的 `chsw` 信封),单片机收到后应把通信模块切到 `body.transport` 指定的通道。若单片机本来就在双通道同时监听,忽略该通知即可。

> 💡 **双端按钮同步**:任一客户端切换成功后,网关会通过 WebSocket 广播 `{"type":"channel","transport":"..."}`(见 §13);同时 `/api/status` 也带 `transport` 字段——两端任一方式都能保持按钮一致。

---

## 12. 历史数据接口

### 12.1 `GET /api/history?limit=N` — 遥测历史(前端曲线)

```
请求: GET http://<网关IP>:8081/api/history?limit=100
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

## 13. WebSocket 实时推送

用于"实时日志"——单片机每发一条上报,网关立刻推给所有已连接的 WS 客户端。

```
连接: ws://<网关IP>:8081/ws
```

### 服务端 → 客户端消息类型

| 类型 | 何时推送 | 格式 |
|---|---|---|
| `mqtt_msg` | 收到单片机上报时 | `{"type":"mqtt_msg","topic":"dev/mcu01/report","payload":"{...}"}` |
| `channel` | 任一客户端切换通道成功后 | `{"type":"channel","transport":"zigbee"}` |
| `mqtt_pub_ack` | 客户端"模拟发布"的确认 | `{"type":"mqtt_pub_ack","ok":true}` |
| `error` | 客户端请求非法 | `{"type":"error","error":"missing_topic"/"mqtt_not_connected"}` |

### 客户端 → 服务端(可选:模拟 MQTT 发布)

```json
{"topic":"dev/mcu01/cmd","payload":"{\"led_on\":1}"}
```

服务端回 `mqtt_pub_ack`。

Qt 用 `QWebSocket` 连接;收到 `mqtt_msg` 追加到日志框,收到 `channel` 更新通道按钮。

---

## 14. MQTT 协议(单片机同事专用)

### 14.1 Topic

```
上行(单片机 → 网关):  dev/mcu01/report   # 传感器数据/状态回执, QoS 1
下行(网关 → 单片机):  dev/mcu01/cmd      # 控制命令, QoS 1
Broker: <网关IP>:1883
```

### 14.2 通用信封(所有 MQTT 报文)

```json
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:00","body":{...}}
```

| 字段 | 说明 |
|---|---|
| `type` | 报文类型:`sensor`(上报)/`status`(回执)/`cmd`(命令)/`chsw`(通道切换通知) |
| `dev` | 固定 `"mcu01"` |
| `ts` | 时间戳字符串 `YYYY-MM-DD HH:MM:SS` |
| `body` | 实际数据 |

### 14.3 传感器上报(type=sensor)

每 2 秒上报一次,4 个字段**可以一起发也可以单独发**:

```json
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:00",
 "body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":2500}}}

// 单独发温度(其余字段网关保持不变):
{"type":"sensor","dev":"mcu01","ts":"2026-08-13 10:00:02",
 "body":{"data":{"temp":26.0}}}
```

### 14.4 状态回执(type=status)—— 执行完命令后回执

执行完控制命令后,回一条 status 回执,网关据此校准执行器真实状态:

```json
{"type":"status","dev":"mcu01","ts":"2026-08-13 10:00:05",
 "body":{"items":[
   {"name":"led","state":"on","value":1},
   {"name":"buzzer","state":"off","value":0}
 ]}}
```

`name` 支持短名(`led`/`motor`/`buzzer`)和长名(`led_on` 等)两套。

### 14.5 控制命令(type=cmd)—— 网关发给你

```json
{"type":"cmd","dev":"mcu01","ts":"2026-08-13 10:00:00",
 "body":{"led_on":1,"led_br":80,"motor_on":0,"motor_sp":50,"motor_dir":0,"buzzer":0}}
```

命令的 6 个字段可能**只含部分**(部分下发),收到哪个字段就执行哪个,没收到的保持原状。

⚠️ **LED 特例**:只要命令里出现 `led_on`,就**一定同时带 `led_br`**(网关保证,取当前亮度,从未设置时默认 50)。单片机固件解析 LED 命令时按"整帧"处理即可,不必单独兜底"只有 led_on 没有 led_br"的情况。其余字段(motor/buzzer 等)仍遵循部分下发语义。

### 14.6 通道切换通知(type=chsw)—— 网关叫你切换通信模块

Web/Qt 端点了通道切换按钮后,网关在切换成功时,**经旧通道**下发一条通知(单片机此刻还挂在旧通道上):

```json
{"type":"chsw","dev":"mcu01","ts":"2026-08-14 15:48:08","body":{"transport":"zigbee"}}
```

| 字段 | 说明 |
|---|---|
| `body.transport` | 要切换到的通道:`"mqtt"` 或 `"zigbee"` |

单片机收到后把通信模块切到指定通道(之后网关的命令都会走新通道);若单片机双通道同时监听,忽略即可。

---

## 15. 错误码约定汇总

| HTTP 状态码 | 场景 | 响应体 |
|---|---|---|
| 200 | 成功 | 各接口见上 |
| 400 | 缺字段 / 非法值 / 非法参数 | `{"error":"missing value"}`、`{"error":"invalid value"}`、`{"status":"error","message":"missing payload"}`、`{"ok":false,"message":"missing/invalid transport"}` |
| 404 | id / 路径不存在 | `{"error":"device_not_found"}` / `{"error":"actuator_not_found"}` / `{"ok":false,"message":"rule_not_found"}` |
| 500 | 内部未就绪(如未推流就录像) | `{"ok":false,"message":"..."}` |
| 503 | 依赖未就绪(MQTT 掉线 / 通道未就绪) | `{"ok":false}` / `{"ok":false,"message":"channel not ready"}` |

---

## 16. 速测命令(curl/wget 直接抄)

> 板上无 curl,用 `wget -q -O-`;示例 IP 换成当前板子 IP。

```bash
# 健康检查 / 版本
wget -q -O- http://<网关IP>:8081/api/health
wget -q -O- http://<网关IP>:8081/api/version

# 设备列表 / 单设备详情
wget -q -O- http://<网关IP>:8081/api/devices
wget -q -O- http://<网关IP>:8081/api/devices/temp_1

# 状态轮询(含 transport 通道字段)
wget -q -O- http://<网关IP>:8081/api/status

# 单执行器控制(开蜂鸣器)
wget -q -O- --post-data='{"value":1}' http://<网关IP>:8081/api/actuators/buzzer_1/set
# 非法值示例(字符串,应回 400)
wget -q -O- --post-data='{"value":"1"}' http://<网关IP>:8081/api/actuators/buzzer_1/set

# 全字段/部分字段控制(只改亮度)
wget -q -O- --post-data='{"type":"control","payload":{"led_br":30}}' --header='Content-Type: application/json' http://<网关IP>:8081/api/control

# 规则:列表 / 热重载 / 停用一条
wget -q -O- http://<网关IP>:8081/api/rules
wget -q -O- --post-data='' http://<网关IP>:8081/api/rules/reload
wget -q -O- --post-data='' http://<网关IP>:8081/api/rules/temp_alarm/disable

# 历史曲线(最近 50 条)
wget -q -O- 'http://<网关IP>:8081/api/history?limit=50'

# 通道:查询 / 切换
wget -q -O- http://<网关IP>:8081/api/channel
wget -q -O- --post-data='{"transport":"zigbee"}' http://<网关IP>:8081/api/channel/switch

# 摄像头:推流 / 抓拍 / 录像 / 状态
wget -q -O- http://<网关IP>:8081/api/camera/start_stream
wget -q -O- http://<网关IP>:8081/api/camera/snapshot
wget -q -O- http://<网关IP>:8081/api/camera/start_record
wget -q -O- http://<网关IP>:8081/api/camera/status
# 取回照片(文件名以 snapshot 接口返回为准)
wget -q -O- 'http://<网关IP>:8081/snapshots/snapshot_xxx.jpg'
```
