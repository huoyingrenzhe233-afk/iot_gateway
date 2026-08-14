#pragma once

// ============================================================
// 通道管理(策略模式):网关↔单片机支持 MQTT/ZigBee 双通道运行时切换
// 职责:
//   1. 记录当前通道(MQTT 默认)
//   2. send_to_mcu(msg) 按当前通道分发(命令下行)
//   3. switch_to() 切换通道(目标通道未就绪则拒绝)
// 线程模型:仅在 mongoose 事件循环线程调用(单线程,无锁)
// ============================================================
#include <string>

namespace gateway {

enum class Transport { MQTT, ZIGBEE };

class MqttClient;
class ZigbeeAdapter; // 前向声明

class ChannelManager {
public:
  // 绑定 MQTT 通道:cmd_topic 是 MQTT 命令下发 topic(dev/mcu01/cmd)
  void set_mqtt(MqttClient *mqtt, const std::string &cmd_topic);
  // 绑定 ZigBee 通道(串口透传适配器)
  void set_zigbee(ZigbeeAdapter *zigbee);
  // 当前通道(MQTT 默认)
  Transport current() const { return current_; }
  // 切换通道;目标通道未就绪返回 false(状态不变)
  bool switch_to(Transport t);
  // 下发命令到单片机(按当前通道);无可用通道返回 false
  bool send_to_mcu(const std::string &envelope);

  // 经"指定通道"下发(不理会当前通道):通道切换时用来在旧通道上
  // 通知单片机切换模块(单片机此刻还挂在旧通道上)。
  bool send_via(Transport t, const std::string &envelope);

private:
  Transport current_ = Transport::MQTT; // 当前通道(MQTT 默认)
  MqttClient *mqtt_ = nullptr;          // MQTT 通道(可空 = 未绑定)
  ZigbeeAdapter *zigbee_ = nullptr;     // ZigBee 通道(可空 = 未绑定)
  std::string cmd_topic_;               // MQTT 命令 topic(dev/mcu01/cmd)
};

} // namespace gateway
