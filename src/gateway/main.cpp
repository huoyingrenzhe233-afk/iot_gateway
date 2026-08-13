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
#include <cstdio>
#include <cstdlib>
#include <mongoose.h>
static const char *VERSION = "1.0.0"; // /api/version 返回的版本号

using gateway::Control;
using gateway::log_level_from_string;
using gateway::Logger;
using gateway::LogLevel;

// HTTP 回调上下文:mongoose 用 fn_data 传给回调(不用全局变量桥接)
// - 监听连接 accept 出的新连接会继承 lsn->fn_data(已查 mongoose.c:5290)
// - 回调里 connect->fn_data 直接拿到这个结构体
struct HttpContext
{
  gateway::Config config;      // 配置副本(值拷贝,回调里只读)
  gateway::MqttClient *mqtt = nullptr; // MQTT 客户端指针(供 /api/control 发布)
  gateway::Device *device = nullptr;   // 设备状态缓存(供 /api/status 读)
  gateway::DeviceRegistry *registry = nullptr; // 设备注册表(供 /api/devices)
};

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
  // 7. 注册收到上报的回调(单片机发 sensor/status 时会触发)
  //    → 更新设备状态缓存(阶段二核心:/api/status 读的就是它)
  g_mqtt.on_message = [](const std::string &topic, const std::string &payload)
  {
    LOG_INFO("on_message: %s -> %s", topic.c_str(), payload.c_str());
    g_device.update_from_report(payload);
  };
  // 8. 启动 HTTP 服务,把 ctx 通过 fn_data 传给回调
  //    HTTP 回调上下文:fn_data 传给 mongoose,回调里经 connect->fn_data 取回
  HttpContext ctx;
  ctx.config = config;
  ctx.mqtt = &g_mqtt;
  ctx.device = &g_device;
  ctx.registry = &g_registry;
  mg_http_listen(&mgr, listen_addr, request_handler, &ctx);
  LOG_INFO("http server listening on :%d", port);

  // 9. 事件循环:轮询所有连接,分发事件到回调(单线程,永不返回)
  for (;;)
  {
    mg_mgr_poll(&mgr, 1000); // 每 1000ms 处理一轮:HTTP/MQTT/定时器
  }
  return 0; // 不可达
}