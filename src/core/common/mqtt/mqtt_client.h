#pragma once

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
  // 发布消息到指定 topic
  void publish(const std::string &topic, const std::string &message);
  // 收到上报时的回调(阶段四接 /api/status 用)
  std::function<void(const std::string &topic, const std::string &payload)>
      on_message;

private:
  static void event_handler(struct mg_connection *c, int ev, void *ev_data);
  void handler_event(struct mg_connection *c, int ev, void *ev_data);
  static void reconnect_timer(void *arg);
  void try_reconnect();

  struct mg_connection *conn = nullptr;
  struct mg_mgr *mgr_ = nullptr;
  struct mg_timer *reconnect_timer_ = nullptr; // 常驻重连定时器,只建一次
  std::string url_, client_id_, sub_topic_;
  bool subscribed_ = false;
};
} // namespace gateway