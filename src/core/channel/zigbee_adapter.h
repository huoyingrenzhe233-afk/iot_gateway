#pragma once
// ============================================================
// ZigBee 适配器(DL-30 无线串口模块,透明透传)
// DL-30 是一对透明无线串口桥:网关往串口写的字节会无线发到单片机侧 DL-30
// 的 RX(反之亦然)。'组网/配对'由 DL-30 自身完成,网关只负责串口收发。
// 协议约定(模块文档):每条消息必须以换行 \n 结尾分帧。
//
// 线程模型:串口 fd 非阻塞(O_NONBLOCK),由 mongoose 的 mg_timer 周期性
// 轮询(50ms)读取,和 MQTT/HTTP 同属单线程事件循环,无并发。
// 为什么不用 mg_wrapfd:mongoose 读路径用 recv()(socket 专用),串口/PTY
// 这类普通 fd 会返回 ENOTSOCK(实测 err 88)→ 只能定时轮询 + read()。
// ============================================================
#include <functional>
#include <string>

struct mg_mgr;
struct mg_timer;

namespace gateway {

class ZigbeeAdapter {
public:
  // 打开串口 + 启动轮询定时器;失败返回 false(模块没插等)
  bool start(struct mg_mgr *mgr, const std::string &device, int baud);
  // 关闭串口(停定时器 + close fd)
  void stop();
  // 发送一条消息到单片机:自动补换行 \n 分帧;串口没开返回 false
  bool send(const std::string &msg);
  // 串口是否已打开(供通道切换判断"zigbee 就绪")
  bool is_open() const { return fd_ >= 0; }
  // 收到单片机上报时的回调(由调用方赋值为统一处理链)
  std::function<void(const std::string &topic, const std::string &payload)> on_message;

private:
  static void poll_timer_cb(void *arg);
  void poll_serial(); // 读串口 + 按 \n 分帧 + 逐条回调

  int fd_ = -1;                       // 串口 fd
  struct mg_mgr *mgr_ = nullptr;      // 所属事件循环(停定时器用)
  struct mg_timer *timer_ = nullptr;  // 轮询定时器(50ms)
  std::string buffer_;                // 未分帧的残留字节缓冲
  int read_fail_count_ = 0;           // 连续读失败计数(日志节流,防拔线后刷屏)
};

} // namespace gateway
