// ============================================================
// 通道管理实现(策略模式):MQTT/ZigBee 双通道运行时切换
// 下行命令按当前通道分发:MQTT → publish(cmd_topic),ZigBee → 串口 send
// 线程模型:仅在 mongoose 事件循环线程调用(单线程,无锁)
// ============================================================
#include "core/channel/channel_manager.h"
#include "core/channel/zigbee_adapter.h"
#include "core/common/logger/logger.h"
#include "core/common/mqtt/mqtt_client.h"

namespace gateway
{

  // ------------------------------------------------------------
  // set_mqtt:绑定 MQTT 通道(保存客户端指针 + 命令 topic)
  // 调用时机:main() 启动时,mqtt 客户端创建后
  // ------------------------------------------------------------
  void ChannelManager::set_mqtt(MqttClient *mqtt, const std::string &cmd_topic)
  {
    mqtt_ = mqtt;
    cmd_topic_ = cmd_topic;
  }

  // ------------------------------------------------------------
  // set_zigbee:绑定 ZigBee 通道(串口透传适配器)
  // 调用时机:main() 启动时,zigbee 适配器创建后
  // ------------------------------------------------------------
  void ChannelManager::set_zigbee(ZigbeeAdapter *zigbee)
  {
    zigbee_ = zigbee;
  }

  // ------------------------------------------------------------
  // switch_to:切换通道
  // 目标通道未就绪返回 false 且状态不变(切换失败不污染当前通道)
  //   MQTT  → 要求 mqtt_ 已绑定
  //   ZIGBEE → 要求 zigbee_ 已绑定且串口已打开(DL-30 插着)
  // ------------------------------------------------------------
  bool ChannelManager::switch_to(Transport t)
  {
    if (t == Transport::ZIGBEE)
    {
      if (zigbee_ == nullptr || !zigbee_->is_open())
      {
        LOG_WARN("channel: switch to zigbee rejected (serial not ready)");
        return false;
      }
    }
    else if (t == Transport::MQTT)
    {
      if (mqtt_ == nullptr)
      {
        LOG_WARN("channel: switch to mqtt rejected (mqtt not bound)");
        return false;
      }
    }
    else
    {
      LOG_WARN("channel: unknown transport");
      return false;
    }
    // 切到和当前相同的通道:幂等成功(没什么可做的)
    if (t != current_)
    {
      LOG_INFO("channel: switch %s -> %s",
               current_ == Transport::ZIGBEE ? "zigbee" : "mqtt",
               t == Transport::ZIGBEE ? "zigbee" : "mqtt");
      current_ = t;
    }
    return true;
  }

  // ------------------------------------------------------------
  // send_to_mcu:下发命令到单片机(按当前通道分发)
  //   MQTT  → mqtt_->publish(cmd_topic_, envelope)
  //   ZIGBEE → zigbee_->send(envelope)(自动补 '\n' 分帧)
  // 当前通道不可用时返回 false(如 MQTT 未连接、串口未打开)
  // ------------------------------------------------------------
  bool ChannelManager::send_to_mcu(const std::string &envelope)
  {
    if (current_ == Transport::ZIGBEE)
    {
      if (zigbee_ == nullptr || !zigbee_->is_open())
      {
        LOG_WARN("channel: zigbee send failed (serial not open)");
        return false;
      }
      return zigbee_->send(envelope);
    }
    // ---- 默认 MQTT 通道 ----
    if (mqtt_ == nullptr)
    {
      LOG_WARN("channel: mqtt send failed (mqtt not bound)");
      return false;
    }
    // MqttClient::publish 在未连接时会静默丢弃(只打日志),这里必须
    // 先查 is_connected(),否则"命令没发出去"也会被当成成功
    if (!mqtt_->is_connected())
    {
      LOG_WARN("channel: mqtt send failed (not connected)");
      return false;
    }
    mqtt_->publish(cmd_topic_, envelope);
    return true;
  }

} // namespace gateway
