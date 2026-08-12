#include "core/common/mqtt/mqtt_client.h"
#include "core/common/logger/logger.h"
#include <mongoose.h>

namespace gateway {
void MqttClient::connect(struct mg_mgr *mgr, const std::string &url,
                         const std::string &client_id,
                         const std::string &sub_topic) {
  // 防重入:已有连接先标记关闭,避免旧连接泄漏
  if (conn != nullptr) {
    conn->is_closing = 1;
    conn = nullptr;
    subscribed_ = false;
  }
  url_ = url;
  mgr_ = mgr;
  client_id_ = client_id;
  sub_topic_ = sub_topic;

  struct mg_mqtt_opts opts = {};
  opts.client_id = mg_str(client_id_.c_str());
  opts.keepalive = 60;
  opts.clean = true;

  conn = mg_mqtt_connect(mgr_, url_.c_str(), &opts, event_handler, this);
  if (conn != nullptr) {
    LOG_INFO("mqtt connecting to %s (client %s)", url_.c_str(),
             client_id_.c_str());
  } else {
    LOG_ERROR("mqtt connect failed: %s", url_.c_str());
  }

  // 常驻重连定时器:只建一次(防止反复断线累积多个 REPEAT 定时器)。
  // 周期触发 → reconnect_timer 检查 conn==nullptr 才重连。
  if (reconnect_timer_ == nullptr) {
    reconnect_timer_ = mg_timer_add(mgr_, 5000, MG_TIMER_REPEAT,
                                    &MqttClient::reconnect_timer, this);
  }
}

void MqttClient::publish(const std::string &topic, const std::string &message) {
  // subscribed_ 当"连接就绪"标志:CONNACK 成功并订阅后才置 true
  if (conn == nullptr || !subscribed_) {
    LOG_WARN("mqtt publish skipped: not connected");
    return;
  }
  struct mg_mqtt_opts opts = {};
  opts.topic = mg_str(topic.c_str());
  opts.message = mg_str(message.c_str());
  opts.qos = 1;
  mg_mqtt_pub(conn, &opts);
  LOG_INFO("mqtt publish %s: %s", topic.c_str(), message.c_str());
}

void MqttClient::event_handler(struct mg_connection *c, int ev, void *ev_data) {
  MqttClient *self = static_cast<MqttClient *>(c->fn_data);
  self->handler_event(c, ev, ev_data);
}

void MqttClient::handler_event(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_MQTT_OPEN) {
    // 注意:mongoose 实现传的是 &mm.ack(uint8_t*),头文件注释写 int* 是错的
    uint8_t ack = *static_cast<uint8_t *>(ev_data);
    if (ack != 0) {
      LOG_ERROR("mqtt CONNACK failed, ack=%d", (int)ack);
      return;
    }
    LOG_INFO("mqtt connected");
    struct mg_mqtt_opts opts = {};
    opts.topic = mg_str(sub_topic_.c_str());
    mg_mqtt_sub(c, &opts);
    subscribed_ = true;
  } else if (ev == MG_EV_MQTT_MSG) {
    struct mg_mqtt_message *mm = static_cast<struct mg_mqtt_message *>(ev_data);
    std::string topic(mm->topic.buf, mm->topic.len);
    std::string payload(mm->data.buf, mm->data.len);
    LOG_INFO("mqtt recv %s: %.*s", topic.c_str(), (int)mm->data.len,
             mm->data.buf);
    if (on_message) {
      on_message(topic, payload);
    }
  } else if (ev == MG_EV_CLOSE) {
    LOG_WARN("mqtt disconnected");
    subscribed_ = false;
    conn = nullptr;
    // 不在此加定时器:常驻 reconnect_timer_ 会发现断线并自动重连,
    // 避免每次断线重复添加 REPEAT 定时器导致定时器累积。
  }
}

void MqttClient::try_reconnect() {
  if (conn == nullptr && mgr_ != nullptr) {
    LOG_WARN("mqtt reconnecting...");
    connect(mgr_, url_, client_id_, sub_topic_);
  }
}
void MqttClient::reconnect_timer(void *arg) {
  MqttClient *self = static_cast<MqttClient *>(arg);
  if (self->conn == nullptr) {
    LOG_WARN("mqtt reconnecting...");
    self->try_reconnect();
  }
}
} // namespace gateway
