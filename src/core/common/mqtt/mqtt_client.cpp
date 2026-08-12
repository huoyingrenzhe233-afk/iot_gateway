#include "core/common/mqtt/mqtt_client.h"
#include "core/common/logger/logger.h"
#include <mongoose.h>

namespace gateway
{

  // ------------------------------------------------------------
  // connect:发起 MQTT 连接,并保证只有一个常驻重连定时器
  // 参数:
  //   mgr      - 所属 mongoose 事件循环(连接挂在这,回调也在这线程跑)
  //   url      - broker 地址,如 "mqtt://127.0.0.1:1883"
  //   client_id - 客户端标识(如 "gateway-1")
  //   sub_topic - 连接成功后订阅的 topic(上报,如 dev/mcu01/report)
  // ------------------------------------------------------------
  void MqttClient::connect(struct mg_mgr *mgr, const std::string &url,
                           const std::string &client_id,
                           const std::string &sub_topic)
  {
    // 防重入:已有连接先标记关闭,避免旧连接泄漏
    if (conn != nullptr)
    {
      conn->is_closing = 1; // 请求 mongoose 关闭旧连接(下次 poll 生效)
      conn = nullptr;       // 本对象立即"忘记"它
      subscribed_ = false;  // 连接状态复位
    }
    // 保存参数:断线重连时要用(url_/client_id_/sub_topic_)
    url_ = url;
    mgr_ = mgr;
    client_id_ = client_id;
    sub_topic_ = sub_topic;

    // 组装 MQTT 连接参数(CONNECT 报文内容)
    struct mg_mqtt_opts opts = {};
    opts.client_id = mg_str(client_id_.c_str());
    opts.keepalive = 60; // 心跳间隔 60s
    opts.clean = true;   // 干净会话(不保留离线消息)

    // 发起连接:event_handler 是回调,this 存进 c->fn_data(回调里取回)
    conn = mg_mqtt_connect(mgr_, url_.c_str(), &opts, event_handler, this);
    if (conn != nullptr)
    {
      LOG_INFO("mqtt connecting to %s (client %s)", url_.c_str(),
               client_id_.c_str());
    }
    else
    {
      LOG_ERROR("mqtt connect failed: %s", url_.c_str());
    }

    // 常驻重连定时器:只建一次(防止反复断线累积多个 REPEAT 定时器)。
    // 周期触发 → reconnect_timer 检查 conn==nullptr 才重连。
    if (reconnect_timer_ == nullptr)
    {
      reconnect_timer_ = mg_timer_add(mgr_, 5000, MG_TIMER_REPEAT,
                                      &MqttClient::reconnect_timer, this);
    }
  }

  // ------------------------------------------------------------
  // publish:发布消息到指定 topic
  // 前置条件:subscribed_ 为 true(已连接且已订阅),否则丢弃并告警
  // ------------------------------------------------------------
  void MqttClient::publish(const std::string &topic, const std::string &message)
  {
    // subscribed_ 当"连接就绪"标志:CONNACK 成功并订阅后才置 true
    if (conn == nullptr || !subscribed_)
    {
      LOG_WARN("mqtt publish skipped: not connected");
      return;
    }
    // 组装发布参数(QoS 1 = 至少一次投递)
    struct mg_mqtt_opts opts = {};
    opts.topic = mg_str(topic.c_str());
    opts.message = mg_str(message.c_str());
    opts.qos = 1;
    mg_mqtt_pub(conn, &opts); // 同步拷贝进发送缓冲,函数返回后 string 可析构
    LOG_INFO("mqtt publish %s: %s", topic.c_str(), message.c_str());
  }

  // ------------------------------------------------------------
  // event_handler:mongoose 静态回调入口
  // mongoose 调用它时,把 c->fn_data 指回本对象,转给成员函数
  // ------------------------------------------------------------
  void MqttClient::event_handler(struct mg_connection *c, int ev, void *ev_data)
  {
    MqttClient *self = static_cast<MqttClient *>(c->fn_data);
    self->handler_event(c, ev, ev_data);
  }

  // ------------------------------------------------------------
  // handler_event:实际处理 MQTT 生命周期事件
  //   MG_EV_MQTT_OPEN - CONNACK 已回:检查 ack,成功后订阅
  //   MG_EV_MQTT_MSG  - 收到发布消息:取出 topic/payload,回调 on_message
  //   MG_EV_CLOSE     - 连接断开:复位状态(常驻定时器会自动重连)
  // ------------------------------------------------------------
  void MqttClient::handler_event(struct mg_connection *c, int ev, void *ev_data)
  {
    if (ev == MG_EV_MQTT_OPEN)
    {
      // 注意:mongoose 实现传的是 &mm.ack(uint8_t*),头文件注释写 int* 是错的
      uint8_t ack = *static_cast<uint8_t *>(ev_data);
      if (ack != 0)
      { // 非 0 = CONNACK 失败(如认证错误)
        LOG_ERROR("mqtt CONNACK failed, ack=%d", (int)ack);
        return;
      }
      LOG_INFO("mqtt connected");
      // 连接成功 → 订阅上报 topic
      struct mg_mqtt_opts opts = {};
      opts.topic = mg_str(sub_topic_.c_str());
      mg_mqtt_sub(c, &opts);
      subscribed_ = true; // 就绪,允许 publish
    }
    else if (ev == MG_EV_MQTT_MSG)
    {
      // 收到单片机上报:把 mongoose 的 mg_str 转成 std::string(拷贝,防缓冲区复用)
      struct mg_mqtt_message *mm = static_cast<struct mg_mqtt_message *>(ev_data);
      std::string topic(mm->topic.buf, mm->topic.len);
      std::string payload(mm->data.buf, mm->data.len);
      LOG_INFO("mqtt recv %s: %.*s", topic.c_str(), (int)mm->data.len,
               mm->data.buf);
      // 交给外部回调(阶段四:解析信封、存库、广播)
      if (on_message)
      {
        on_message(topic, payload);
      }
    }
    else if (ev == MG_EV_CLOSE)
    {
      LOG_WARN("mqtt disconnected");
      subscribed_ = false;
      conn = nullptr;
      // 不在此加定时器:常驻 reconnect_timer_ 会发现断线并自动重连,
      // 避免每次断线重复添加 REPEAT 定时器导致定时器累积。
    }
  }

  // ------------------------------------------------------------
  // try_reconnect:断线后重连(仅当确实没连接且事件循环还在)
  // ------------------------------------------------------------
  void MqttClient::try_reconnect()
  {
    if (conn == nullptr && mgr_ != nullptr)
    {
      LOG_WARN("mqtt reconnecting...");
      connect(mgr_, url_, client_id_, sub_topic_);
    }
  }

  // ------------------------------------------------------------
  // reconnect_timer:定时器回调(每 5s 触发一次)
  // arg 指向 MqttClient 实例(定时器创建时传入的 this)
  // ------------------------------------------------------------
  void MqttClient::reconnect_timer(void *arg)
  {
    MqttClient *self = static_cast<MqttClient *>(arg);
    if (self->conn == nullptr)
    { // 断线了才重连,连接正常就不打扰
      self->try_reconnect();
    }
  }
} // namespace gateway
