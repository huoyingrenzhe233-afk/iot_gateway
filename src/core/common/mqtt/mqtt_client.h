#pragma once

// ============================================================
// MQTT 客户端封装(mongoose 内置 MQTT)
// 职责:
//   1. 连接 mosquitto broker
//   2. 订阅上报 topic(dev/mcu01/report),收到数据回调 on_message
//   3. publish() 发布命令到指定 topic(dev/mcu01/cmd)
//   4. 断线自动重连(常驻定时器,5s 一次)
//   5. keepalive 心跳(常驻定时器,30s 发一次 PINGREQ,兑现 CONNECT 里
//      keepalive=60 的承诺,防止 broker 90s 无消息强制断开)
// 线程模型:所有方法必须在 mongoose 事件循环线程调用(单线程)
// ============================================================
#include <functional>
#include <mongoose.h>
#include <string>

namespace gateway {
class MqttClient {
public:
  // 连接:url 如 "mqtt://127.0.0.1:1883";sub_topic 为连接成功后订阅的上报
  // topic(来自 Config,如 dev/mcu01/report)
  void connect(struct mg_mgr *mgr, const std::string &url,
               const std::string &client_id, const std::string &sub_topic);
  // 发布消息到指定 topic(仅"已连接且已订阅"时才真正发送)
  void publish(const std::string &topic, const std::string &message);
  // 是否已连接且已订阅(发布前置条件;/api/actuators/:id/set 的 503 判断用)
  bool is_connected() const { return conn != nullptr && subscribed_; }
  // 收到上报时的回调(阶段四接 /api/status 用),由调用方赋值
  std::function<void(const std::string &topic, const std::string &payload)>
      on_message;

private:
  // mongoose 静态回调:转发给成员函数(经 c->fn_data 拿 this)
  static void event_handler(struct mg_connection *c, int ev, void *ev_data);
  // 实际事件处理(MQTT_OPEN/MSG/CLOSE)
  void handler_event(struct mg_connection *c, int ev, void *ev_data);
  // 定时器回调:周期检查断线,触发重连
  static void reconnect_timer(void *arg);
  // 定时器回调:周期发送 MQTT PINGREQ 心跳(兑现 keepalive 承诺)
  static void ping_timer(void *arg);
  // 尝试重连(用保存的 url_/client_id_/sub_topic_)
  void try_reconnect();

  struct mg_connection *conn = nullptr;      // 当前 MQTT 连接(null = 未连接)
  struct mg_mgr *mgr_ = nullptr;             // 所属事件循环(重连要用)
  struct mg_timer *reconnect_timer_ = nullptr; // 常驻重连定时器,只建一次
  struct mg_timer *ping_timer_ = nullptr;      // 常驻心跳定时器,只建一次
  std::string url_, client_id_, sub_topic_;  // 保存连接参数,供重连使用
  bool subscribed_ = false;                  // 已连接且已订阅(发布前置条件)
};
} // namespace gateway