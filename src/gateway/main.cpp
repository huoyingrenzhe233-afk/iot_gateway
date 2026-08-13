// ============================================================
// 网关主程序入口(阶段 1-3)
// 职责:启动 HTTP 服务(mongoose)+ MQTT 客户端,路由 /api/* 请求
// 数据流:Web/Qt --HTTP--> 网关 --MQTT--> 单片机
//       单片机 --MQTT--> 网关 --内存缓存/WS--> Web/Qt
// ============================================================
#include "core/common/config/config.h"   // 配置加载(读 config/gateway.yaml)
#include "core/common/logger/logger.h"   // 日志
#include "core/common/mqtt/mqtt_client.h" // MQTT 客户端
#include "core/control/control.h"        // 控制信封组包
#include "core/device/device.h"          // 设备状态管理(6 外设缓存)
#include "core/device/device_registry.h" // 设备注册表(静态登记)
#include "core/rules/rule_engine.h"      // 规则引擎(阶段七:/api/rules 系列 + 上报触发评估)
#include <cstdio>
#include <cstdlib>
#include <mongoose.h>
static const char *VERSION = "1.0.0"; // /api/version 返回的版本号
static const char *kRulesPath = "config/rules/rules.yaml"; // 规则配置文件路径(阶段七,reload 时也用它)

using gateway::Control;
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
  mg_http_listen(&mgr, listen_addr, request_handler, &ctx);
  LOG_INFO("http server listening on :%d", port);

  // 9. 事件循环:轮询所有连接,分发事件到回调(单线程,永不返回)
  for (;;)
  {
    mg_mgr_poll(&mgr, 1000); // 每 1000ms 处理一轮:HTTP/MQTT/定时器
  }
  return 0; // 不可达
}