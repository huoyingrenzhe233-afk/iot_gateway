// ============================================================
// 网关主程序入口(阶段 1-8 全部集成)
// 职责:启动 HTTP 服务(mongoose)+ MQTT 客户端,路由 /api/* 请求,
//       集成设备状态/注册表/规则引擎/摄像头管理
// 数据流:Web/Qt --HTTP--> 网关 --MQTT--> 单片机
//       单片机 --MQTT--> 网关 --内存缓存--> Web/Qt(WS 推送待做)
// ============================================================
#include "core/common/config/config.h"   // 配置加载(读 config/gateway.yaml)
#include "core/common/logger/logger.h"   // 日志
#include "core/common/mqtt/mqtt_client.h" // MQTT 客户端
#include "core/control/control.h"        // 控制信封组包
#include "core/camera/camera_manager.h"  // 摄像头管理(方案 C:mjpg-streamer MJPEG)
#include "core/device/device.h"          // 设备状态管理(6 外设缓存)
#include "core/device/device_registry.h" // 设备注册表(静态登记)
#include "core/rules/rule_engine.h"      // 规则引擎(阶段七:/api/rules 系列 + 上报触发评估)
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h> // waitpid:僵尸进程收割(reap_children)
#include <mongoose.h>
static const char *VERSION = "1.0.0"; // /api/version 返回的版本号
static const char *kRulesPath = "config/rules/rules.yaml"; // 规则配置文件路径(阶段七,reload 时也用它)

using gateway::Control;
using gateway::CameraManager;
using gateway::log_level_from_string;
using gateway::Logger;
using gateway::LogLevel;
using gateway::RuleEngine;

// HTTP 回调上下文:mongoose 用 fn_data 传给回调(不用全局变量桥接)
// - 监听连接 accept 出的新连接会继承 lsn->fn_data(已查 mongoose.c:5290)
// - 回调里 connect->fn_data 直接拿到这个结构体
struct HttpContext
{
  gateway::Config config;      // 配置副本(值拷贝,回调里只读)
  gateway::MqttClient *mqtt = nullptr; // MQTT 客户端指针(供 /api/control 发布)
  gateway::Device *device = nullptr;   // 设备状态缓存(供 /api/status 读)
  gateway::DeviceRegistry *registry = nullptr; // 设备注册表(供 /api/devices)
  gateway::RuleEngine *rules = nullptr; // 规则引擎(供 /api/rules 系列路由)
  gateway::CameraManager *camera = nullptr; // 摄像头管理(供 /api/camera/* 路由)
};

// ------------------------------------------------------------
// extract_rule_id:从 /api/rules/<id>/<suffix> URI 里抠出 <id>
//   例:POST /api/rules/temp_alarm/enable → suffix="/enable",out="temp_alarm"
// 三个条件都满足才返回 true:
//   1. URI 比 <prefix> + <suffix> 长(至少夹一个 id 字符)
//   2. URI 以 /api/rules/ 开头
//   3. URI 以 suffix(/enable 或 /disable)结尾
// 中间的字符串就是规则 id;id 为空返回 false
// ------------------------------------------------------------
static bool extract_rule_id(const struct mg_http_message *hm,
                            const char *suffix, std::string &out)
{
  std::string uri(hm->uri.buf, hm->uri.len);
  const std::string prefix = "/api/rules/";
  if (uri.size() <= prefix.size() + strlen(suffix)) return false;
  if (uri.compare(0, prefix.size(), prefix) != 0) return false;
  if (uri.compare(uri.size() - strlen(suffix), strlen(suffix), suffix) != 0)
    return false;
  out = uri.substr(prefix.size(), uri.size() - prefix.size() - strlen(suffix));
  return !out.empty();
}

// ------------------------------------------------------------
// extract_device_id:从 /api/devices/<id> URI 里抠出 <id>
//   例:GET /api/devices/temp_1 → out="temp_1"
// ------------------------------------------------------------
static bool extract_device_id(const struct mg_http_message *hm, std::string &out)
{
  std::string uri(hm->uri.buf, hm->uri.len);
  const std::string prefix = "/api/devices/";
  if (uri.size() <= prefix.size()) return false;
  if (uri.compare(0, prefix.size(), prefix) != 0) return false;
  out = uri.substr(prefix.size());
  return !out.empty();
}

// ------------------------------------------------------------
// extract_actuator_id:从 /api/actuators/<id>/set URI 里抠出 <id>
//   例:POST /api/actuators/led_1/set → out="led_1"
// ------------------------------------------------------------
static bool extract_actuator_id(const struct mg_http_message *hm, std::string &out)
{
  std::string uri(hm->uri.buf, hm->uri.len);
  const std::string prefix = "/api/actuators/";
  const std::string suffix = "/set";
  if (uri.size() <= prefix.size() + suffix.size()) return false;
  if (uri.compare(0, prefix.size(), prefix) != 0) return false;
  if (uri.compare(uri.size() - suffix.size(), suffix.size(), suffix) != 0)
    return false;
  out = uri.substr(prefix.size(), uri.size() - prefix.size() - suffix.size());
  return !out.empty();
}

// ------------------------------------------------------------
// actuator_primary_field:执行器 id → 主命令字段映射
//   led_1 → led_on、motor_1 → motor_on、buzzer_1 → buzzer
// 与 rule_engine.cpp 的 actuator_fields() 白名单一致;未知 id 返回 nullptr。
// (为什么只映射"主"字段:老师验收示例 POST /api/actuators/led/set {"value":1}
//  语义是"开/关该执行器",取每个执行器最直观的开关字段)
// ------------------------------------------------------------
static const char *actuator_primary_field(const std::string &id)
{
  if (id == "led_1")    return "led_on";
  if (id == "motor_1")  return "motor_on";
  if (id == "buzzer_1") return "buzzer";
  return nullptr;
}

// ------------------------------------------------------------
// 僵尸进程收割:每 5s 回收已退出的子进程(mjpg_streamer/ffmpeg/wget)
// fork 出的子进程退出后若没人 waitpid 会留僵尸占进程表;
// 事件循环单线程,这里用 waitpid(-1, WNOHANG) 非阻塞轮询一把收干净
// ------------------------------------------------------------
static void reap_children(void *arg)
{
  (void)arg; // 回调签名要求 void* 参数,这里用不到
  while (waitpid(-1, NULL, WNOHANG) > 0)
  {
    // 循环收割,直到没有已退出子进程为止
  }
}

// ------------------------------------------------------------
// HTTP 请求处理器:mongoose 每个 HTTP 请求都会回调这里
// connect   = 当前请求的连接(可回写响应)
// event     = 事件类型(我们只关心 MG_EV_HTTP_MSG = 收到完整 HTTP 请求)
// event_data = 事件数据,对 HTTP_MSG 是 struct mg_http_message* (含 method/uri/body)
// ------------------------------------------------------------
static void request_handler(struct mg_connection *connect, int event,
                            void *event_data)
{
  if (event == MG_EV_HTTP_MSG)
  {
    struct mg_http_message *hm = (struct mg_http_message *)event_data;
    // 从 mongoose 连接取出上下文(fn_data 方式,替代全局变量)
    HttpContext *ctx = static_cast<HttpContext *>(connect->fn_data);
    LOG_INFO("HTTP %.*s %.*s", (int)hm->method.len, hm->method.buf,
             (int)hm->uri.len, hm->uri.buf);

    // enable/disable 路由共用:从 /api/rules/<id>/<suffix> 抠出的规则 id
    std::string rule_id;
    // devices/actuators 路由共用:从 URI 抠出的设备/执行器 id
    std::string device_id_str;
    std::string actuator_id_str;

    // ---- 路由表:按 URI + method 匹配(防 POST/DELETE 误触发 GET 端点) ----

    // GET /api/version → 返回版本号
    if (mg_match(hm->uri, mg_str("/api/version"), NULL) &&
        mg_match(hm->method, mg_str("GET"), NULL))
    {
      mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                    "{\"version\":\"%s\"}", VERSION);
    }

    // GET /api/health → 健康检查(存活探针)
    else if (mg_match(hm->uri, mg_str("/api/health"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    }

    // GET /api/devices → 设备列表(注册表,老师验收:至少 3 传感器+3 执行器)
    else if (mg_match(hm->uri, mg_str("/api/devices"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      if (ctx->registry != nullptr)
      {
        std::string list = ctx->registry->to_json_list();
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "%s", list.c_str());
      }
      else
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"registry_not_ready\"}");
      }
    }

    // GET /api/devices/<id> → 单设备详情(老师验收 3.2.5#2:返回单个设备详情)
    // online/last_seen 来自 Device 缓存(收到过上报 = 在线)
    else if (mg_match(hm->method, mg_str("GET"), NULL) &&
             extract_device_id(hm, device_id_str))
    {
      std::string last_seen =
          (ctx->device != nullptr) ? ctx->device->last_seen() : "";
      std::string detail =
          (ctx->registry != nullptr)
              ? ctx->registry->to_json_detail(device_id_str, last_seen)
              : "";
      if (detail.empty())
      {
        mg_http_reply(connect, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"device_not_found\"}");
      }
      else
      {
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "%s", detail.c_str());
      }
    }

    // POST /api/actuators/<id>/set → 单执行器下发(老师验收 3.2.5#5)
    // body {"value":1};id → 主命令字段 → 组信封 → 发布 + 缓存同步
    // 状态码:200 成功 / 404 未知执行器 / 400 缺 value / 503 MQTT 未连接
    else if (mg_match(hm->method, mg_str("POST"), NULL) &&
             extract_actuator_id(hm, actuator_id_str))
    {
      const char *field = actuator_primary_field(actuator_id_str);
      if (field == nullptr)
      {
        mg_http_reply(connect, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"actuator_not_found\"}");
      }
      else if (mg_json_get_tok(mg_str_n(hm->body.buf, hm->body.len), "$.value")
                   .len == 0)
      {
        mg_http_reply(connect, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"missing value\"}");
      }
      else if (ctx->mqtt == nullptr || !ctx->mqtt->is_connected())
      {
        mg_http_reply(connect, 503, "Content-Type: application/json\r\n",
                      "{\"ok\":false}");
      }
      else
      {
        long value = mg_json_get_long(mg_str_n(hm->body.buf, hm->body.len),
                                      "$.value", 0);
        std::string envelope = Control::build_field_envelope(
            ctx->config.device_id, field, value);
        ctx->mqtt->publish(ctx->config.mqtt_topic_cmd, envelope);
        if (ctx->device != nullptr)
        {
          ctx->device->update_from_control(envelope); // 同步缓存,UI 秒响应
        }
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true}");
      }
    }

    // GET /api/status → 设备状态聚合(Web/Qt 轮询接口)
    else if (mg_match(hm->uri, mg_str("/api/status"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      if (ctx->device != nullptr)
      {
        std::string status = ctx->device->get_status_json();
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "%s", status.c_str());
      }
      else
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"device not ready\"}");
      }
    }

    // POST /api/control → 控制命令下行(核心)
    // 要求:method 必须是 POST(防止 GET 误触发)
    else if (mg_match(hm->uri, mg_str("/api/control"), NULL) &&
             mg_match(hm->method, mg_str("POST"), NULL))
    {
      Control control; // 组包工具(见 control.cpp)
      // 组 MQTT 信封:device_id 从 ctx->config 传进来(fn_data 拿到)
      std::string envelope =
          control.build_control_envelope(
              std::string(hm->body.buf, hm->body.len), ctx->config.device_id);
      if (envelope.empty())
      {
        // 组包失败(请求体里没有 body 字段)→ 400
        mg_http_reply(connect, 400, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"missing payload\"}");
        return;
      }
      // 发布到 MQTT 的 dev/mcu01/cmd topic,单片机收到后执行
      if (ctx->mqtt != nullptr)
      {
        ctx->mqtt->publish(ctx->config.mqtt_topic_cmd, envelope);
        // 同步到状态缓存:让 /api/status 立即反映新命令状态(不必等回执)
        if (ctx->device != nullptr)
        {
          ctx->device->update_from_control(envelope);
        }
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
      }
      else
      {
        // MQTT 还没初始化(理论上不会发生,防御性检查)
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"mqtt not ready\"}");
      }
    }

    // ---- 规则引擎路由(阶段七,老师验收 4 个 API) ----
    // 顺序铁律:reload 必须排在 :id/enable、:id/disable 之前,
    // 否则 "reload" 会被 extract_rule_id 误当成规则 id 解析。

    // GET /api/rules → 规则列表(老师验收:规则引擎 4 API)
    else if (mg_match(hm->uri, mg_str("/api/rules"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      if (ctx->rules != nullptr)
      {
        std::string list = ctx->rules->to_json_list();
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      "%s", list.c_str());
      }
      else
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"rules_not_ready\"}");
      }
    }

    // POST /api/rules/reload → 重载规则(老师要求的运行时改配置入口)
    // 从磁盘重读 rules.yaml,保留已启用/停用状态
    else if (mg_match(hm->uri, mg_str("/api/rules/reload"), NULL) &&
             mg_match(hm->method, mg_str("POST"), NULL))
    {
      if (ctx->rules != nullptr && ctx->registry != nullptr)
      {
        bool ok = ctx->rules->reload(kRulesPath, *ctx->registry);
        mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}"
                         : "{\"ok\":false,\"message\":\"reload failed\"}");
      }
      else
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"rules_not_ready\"}");
      }
    }

    // POST /api/rules/<id>/enable → 启用某条规则(老师验收:规则引擎 4 API)
    else if (mg_match(hm->method, mg_str("POST"), NULL) &&
             extract_rule_id(hm, "/enable", rule_id))
    {
      bool ok = (ctx->rules != nullptr) && ctx->rules->set_enabled(rule_id, true);
      mg_http_reply(connect, ok ? 200 : 404, "Content-Type: application/json\r\n",
                    ok ? "{\"ok\":true}"
                       : "{\"ok\":false,\"message\":\"rule_not_found\"}");
    }

    // POST /api/rules/<id>/disable → 停用某条规则(和 enable 对称)
    else if (mg_match(hm->method, mg_str("POST"), NULL) &&
             extract_rule_id(hm, "/disable", rule_id))
    {
      bool ok = (ctx->rules != nullptr) && ctx->rules->set_enabled(rule_id, false);
      mg_http_reply(connect, ok ? 200 : 404, "Content-Type: application/json\r\n",
                    ok ? "{\"ok\":true}"
                       : "{\"ok\":false,\"message\":\"rule_not_found\"}");
    }

    // ---- 摄像头路由(方案 C:mjpg-streamer 推流 + wget 抓拍 + ffmpeg 录像) ----
    // 方法同时接受 GET 和 POST:老师给的 HTML 用 GET 直连,
    // plan.md 写的是 POST —— 两种都认,避免前端联调踩坑

    // GET/POST /api/camera/start_stream → 启动推流(fork mjpg_streamer)
    else if (mg_match(hm->uri, mg_str("/api/camera/start_stream"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      if (ctx->camera == nullptr)
      {
        // 摄像头管理未初始化(防御性检查,理论上不会发生)
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"start_stream failed\"}");
      }
      else
      {
        bool ok = ctx->camera->start_stream();
        mg_http_reply(connect, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}"
                         : "{\"ok\":false,\"message\":\"start_stream failed\"}");
      }
    }

    // GET/POST /api/camera/stop_stream → 停止推流(kill + waitpid)
    else if (mg_match(hm->uri, mg_str("/api/camera/stop_stream"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      if (ctx->camera == nullptr)
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"stop_stream failed\"}");
      }
      else
      {
        bool ok = ctx->camera->stop_stream();
        mg_http_reply(connect, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}"
                         : "{\"ok\":false,\"message\":\"stop_stream failed\"}");
      }
    }

    // GET/POST /api/camera/start_record → 开始录像(fork ffmpeg -c copy)
    else if (mg_match(hm->uri, mg_str("/api/camera/start_record"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      if (ctx->camera == nullptr)
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"start_record failed\"}");
      }
      else
      {
        bool ok = ctx->camera->start_record();
        mg_http_reply(connect, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}"
                         : "{\"ok\":false,\"message\":\"start_record failed\"}");
      }
    }

    // GET/POST /api/camera/stop_record → 停止录像(和 stop_stream 对称)
    else if (mg_match(hm->uri, mg_str("/api/camera/stop_record"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      if (ctx->camera == nullptr)
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"stop_record failed\"}");
      }
      else
      {
        bool ok = ctx->camera->stop_record();
        mg_http_reply(connect, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}"
                         : "{\"ok\":false,\"message\":\"stop_record failed\"}");
      }
    }

    // GET/POST /api/camera/snapshot → 抓拍一帧(wget 抓 ?action=snapshot)
    // 成功返回文件名,前端可以拼 http://<host>:8081/snapshots/<文件名> 展示
    else if (mg_match(hm->uri, mg_str("/api/camera/snapshot"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      if (ctx->camera == nullptr)
      {
        mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"message\":\"snapshot failed\"}");
      }
      else
      {
        std::string fn;
        bool ok = ctx->camera->snapshot(fn);
        if (ok)
        {
          mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                        "{\"ok\":true,\"filename\":\"%s\"}", fn.c_str());
        }
        else
        {
          mg_http_reply(connect, 500, "Content-Type: application/json\r\n",
                        "{\"ok\":false,\"message\":\"snapshot failed\"}");
        }
      }
    }

    // GET/POST /api/camera/status → 状态查询(推流/录像是否在跑)
    else if (mg_match(hm->uri, mg_str("/api/camera/status"), NULL) &&
             (mg_match(hm->method, mg_str("GET"), NULL) ||
              mg_match(hm->method, mg_str("POST"), NULL)))
    {
      bool running = (ctx->camera != nullptr) && ctx->camera->stream_running();
      bool recording = (ctx->camera != nullptr) && ctx->camera->record_running();
      mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                    "{\"running\":%s,\"recording\":%s}",
                    running ? "true" : "false", recording ? "true" : "false");
    }

    // GET / 或 /index.html → 伺服 web/index.html(前端页面)
    // 前端页面由网关伺服,浏览器开 http://<板子IP>:8081/ 即可;
    // 页面里 <img> 直连 8080 的 mjpg-streamer 流,网关不代理视频数据
    else if (mg_match(hm->uri, mg_str("/"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      struct mg_http_serve_opts opts = {};
      opts.root_dir = "web"; // 资源根目录(必须非空,见 mongoose.h:1783)
      mg_http_serve_file(connect, hm, "web/index.html", &opts);
    }
    else if (mg_match(hm->uri, mg_str("/index.html"), NULL) &&
             mg_match(hm->method, mg_str("GET"), NULL))
    {
      struct mg_http_serve_opts opts = {};
      opts.root_dir = "web";
      mg_http_serve_file(connect, hm, "web/index.html", &opts);
    }

    // 其他一切路径 → 404
    else
    {
      LOG_WARN("404: %.*s", (int)hm->uri.len, hm->uri.buf);
      mg_http_reply(connect, 404, "", "NOT_FOUND");
    }
  }
}

// ------------------------------------------------------------
// 主函数:初始化 → 启动服务 → 进入事件循环(永不退出)
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
  // 1. 加载配置(config/gateway.yaml)→ Config 结构体
  gateway::Config config = gateway::load_config("config/gateway.yaml");
  // 2. 初始化日志(写 gateway.log,级别从配置读)
  gateway::Logger::instance().init("gateway.log",
                                   log_level_from_string(config.log_level));
  LOG_INFO("gateway starting");

  // 3. 监听端口:优先取命令行 argv[1],否则用配置的 server.port
  //    (板上 8080 被 mjpg_streamer 摄像头占用,所以部署时传 8081)
  int port = config.server_port;
  if (argc > 1)
  {
    int p = std::atoi(argv[1]);
    if (p > 0 && p <= 65535)
    {
      port = p;
    }
  }
  char listen_addr[64];
  std::snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", port);

  // 4. 初始化 mongoose 事件管理器(所有连接都挂在这个 mgr 上)
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  // 5. 创建 MQTT 客户端并连接板上 mosquitto
  //    static 常驻:生命周期覆盖整个事件循环(避免回调野指针)
  static gateway::MqttClient g_mqtt; // 或局部变量
  g_mqtt.connect(&mgr, config.mqtt_broker, "gateway-1",
                 config.mqtt_topic_report);
  // 6. 设备状态缓存(static 常驻,on_message 写、/api/status 读,同线程安全)
  static gateway::Device g_device;
  // 6.5 设备注册表(static 常驻,启动时从 config/devices/*.yaml 静态登记)
  static gateway::DeviceRegistry g_registry;
  g_registry.load("config/devices/sensors.yaml", "config/devices/actuators.yaml");
  LOG_INFO("device registry loaded: %zu entries", g_registry.size());
  // 6.6 规则引擎(static 常驻,和 g_device/g_mqtt 一致:生命周期覆盖事件循环)
  static gateway::RuleEngine g_rules;
  g_rules.set_device_id(config.device_id);
  bool rules_ok = g_rules.load(kRulesPath, g_registry);
  LOG_INFO("rules loaded: %zu entries (%s)", g_rules.size(),
           rules_ok ? "ok" : "FAILED");
  // 6.7 摄像头管理(static 常驻,方案 C:mjpg-streamer 推流 + wget 抓拍 + ffmpeg 录像)
  //     fork 出的子进程(mjpg_streamer/ffmpeg/wget)由 reap_children 定时器收割
  static gateway::CameraManager g_camera;
  g_camera.set_config(config.camera_device, config.camera_port);
  LOG_INFO("camera: device=%s port=%d", config.camera_device.c_str(),
           config.camera_port);
  // 6.8 僵尸进程收割定时器:每 5s 回收一次已退出子进程(不阻塞事件循环)
  mg_timer_add(&mgr, 5000, MG_TIMER_REPEAT, reap_children, nullptr); // 僵尸收割
  // 7. 注册收到上报的回调(单片机发 sensor/status 时会触发)
  //    → 更新设备状态缓存(阶段二核心:/api/status 读的就是它)
  g_mqtt.on_message = [](const std::string &topic, const std::string &payload)
  {
    LOG_INFO("on_message: %s -> %s", topic.c_str(), payload.c_str());
    g_device.update_from_report(payload);
    g_rules.evaluate(payload); // 触发规则引擎评估(仅 sensor 信封生效)
  };
  // 7.5 规则动作回调:规则触发 → 发布命令到单片机 + 同步状态缓存
  //     和 /api/control 同款逻辑(发布 + update_from_control 两步)
  //     main 永不返回、事件循环单线程,[&] 捕获的引用生命周期足够安全
  g_rules.on_action = [&](const std::string &envelope) {
    // 规则触发:发布命令到单片机 + 同步状态缓存(和 /api/control 同款逻辑)
    LOG_INFO("rule action: %s", envelope.c_str());
    g_mqtt.publish(config.mqtt_topic_cmd, envelope);
    g_device.update_from_control(envelope);
  };
  // 8. 启动 HTTP 服务,把 ctx 通过 fn_data 传给回调
  //    HTTP 回调上下文:fn_data 传给 mongoose,回调里经 connect->fn_data 取回
  HttpContext ctx;
  ctx.config = config;
  ctx.mqtt = &g_mqtt;
  ctx.device = &g_device;
  ctx.registry = &g_registry;
  ctx.rules = &g_rules;
  ctx.camera = &g_camera;
  mg_http_listen(&mgr, listen_addr, request_handler, &ctx);
  LOG_INFO("http server listening on :%d", port);

  // 9. 事件循环:轮询所有连接,分发事件到回调(单线程,永不返回)
  for (;;)
  {
    mg_mgr_poll(&mgr, 1000); // 每 1000ms 处理一轮:HTTP/MQTT/定时器
  }
  return 0; // 不可达
}