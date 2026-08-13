// ============================================================
// ZigBee 适配器实现(DL-30 无线串口模块,透明透传)
// 实现要点:
//   - POSIX termios 配置串口(115200 8N1,无流控,非阻塞)
//   - 用 mg_timer 50ms 轮询非阻塞串口 fd(mongoose 读路径只认 socket,
//     recv() 对串口会 ENOTSOCK,所以不能 mg_wrapfd,只能定时轮询 + read)
//   - 按 '\n' 分帧(模块文档:每条消息必须以换行结尾),兼容 '\r\n'
// 线程模型:所有方法都在 mongoose 事件循环线程调用,无锁无并发
// ============================================================
#include "core/channel/zigbee_adapter.h"
#include "core/common/logger/logger.h"
#include <mongoose.h> // mg_timer_add/mg_timer_free/MG_TIMER_REPEAT
#include <cerrno>     // EAGAIN/EWOULDBLOCK
#include <cstring>    // strerror
#include <fcntl.h>    // open/O_RDWR/O_NOCTTY/O_NONBLOCK
#include <termios.h>  // termios/cfmakeraw/cfsetispeed/cfsetospeed
#include <unistd.h>   // read/write/close

namespace gateway {

// ------------------------------------------------------------
// speed_from_baud:波特率数值 → termios 波特率常量
// 只映射 DL-30 常用的几档;不认识的默认 115200(与 DL-30 出厂一致)
// ------------------------------------------------------------
static speed_t speed_from_baud(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B115200;
  }
}

// ------------------------------------------------------------
// start:打开串口 + 启动 50ms 轮询定时器
// 失败场景:设备节点不存在(DL-30 没插)、打开被拒等 → 返回 false,
// 由调用方决定只告警不退出(切 zigbee 通道时才要求就绪)
// ------------------------------------------------------------
bool ZigbeeAdapter::start(struct mg_mgr *mgr, const std::string &device, int baud) {
  if (fd_ >= 0) return true; // 已打开,幂等返回成功

  // O_NONBLOCK:read 无数据立即返回(EAGAIN),轮询定时器里不阻塞事件循环
  fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    LOG_ERROR("zigbee: open %s failed: %s (DL-30 not plugged?)",
              device.c_str(), strerror(errno));
    return false;
  }
  // termios 配置:原始模式 + 8N1 + 无流控(经典串口透明透传设置)
  struct termios tty;
  if (tcgetattr(fd_, &tty) != 0) {
    LOG_ERROR("zigbee: tcgetattr failed: %s", strerror(errno));
    close(fd_); fd_ = -1;
    return false;
  }
  cfmakeraw(&tty);                                  // 原始模式:字节原样透传
  cfsetispeed(&tty, speed_from_baud(baud));         // 输入波特率
  cfsetospeed(&tty, speed_from_baud(baud));         // 输出波特率
  tty.c_cflag |= (CLOCAL | CREAD);                  // CLOCAL:忽略调制解调器;CREAD:启用接收
  tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);      // 8N1:无校验/1停止位/无硬件流控
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;                               // 8 位数据位
  tty.c_cc[VMIN] = 0;                               // 非阻塞:没数据立即返回
  tty.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    LOG_ERROR("zigbee: tcsetattr failed: %s", strerror(errno));
    close(fd_); fd_ = -1;
    return false;
  }
  mgr_ = mgr;
  // 50ms 轮询:DL-30 数据量小(传感器几秒一条),50ms 延迟完全够;
  // 读循环一次 read 干净所有可用字节,突发也不丢
  timer_ = mg_timer_add(mgr_, 50, MG_TIMER_REPEAT, &ZigbeeAdapter::poll_timer_cb, this);
  LOG_INFO("zigbee: opened %s @ %d baud", device.c_str(), baud);
  return true;
}

// ------------------------------------------------------------
// stop:关闭串口(停轮询定时器 + close fd)
// ------------------------------------------------------------
void ZigbeeAdapter::stop() {
  if (timer_ != nullptr && mgr_ != nullptr) {
    mg_timer_free(&mgr_->timers, timer_); // 从事件循环摘掉轮询定时器
    timer_ = nullptr;
  }
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  buffer_.clear(); // 清掉未分帧的残留数据
  LOG_INFO("zigbee: stopped");
}

// ------------------------------------------------------------
// send:发送一条消息到单片机
// 协议约定:每条消息必须以 '\n' 结尾(模块文档"发送的信息都要有换行"),
// 这里自动补上,调用方不用关心分帧
// ------------------------------------------------------------
bool ZigbeeAdapter::send(const std::string &msg) {
  if (fd_ < 0) {
    LOG_WARN("zigbee: send skipped, serial not open");
    return false;
  }
  std::string frame = msg + "\n"; // 自动补换行分帧
  size_t off = 0;                 // 已写入的偏移
  // write 对串口可能部分写入(缓冲满等),循环写直到全部发出或出错
  while (off < frame.size()) {
    ssize_t n = write(fd_, frame.data() + off, frame.size() - off);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // 非阻塞缓冲满:重试(单线程,不会死等,但极端情况会空转)
      }
      LOG_ERROR("zigbee: write failed: %s", strerror(errno));
      return false;
    }
    if (n == 0) break; // 写 0 字节:异常,放弃剩余
    off += static_cast<size_t>(n);
  }
  return off == frame.size(); // 全部写完才算成功
}

// ------------------------------------------------------------
// poll_timer_cb:轮询定时器回调(每 50ms 一次)
// arg 是 start() 里传入的 this,转给成员函数
// ------------------------------------------------------------
void ZigbeeAdapter::poll_timer_cb(void *arg) {
  ZigbeeAdapter *self = static_cast<ZigbeeAdapter *>(arg);
  self->poll_serial();
}

// ------------------------------------------------------------
// poll_serial:读串口 + 按 '\n' 分帧 + 逐条回调
// 读循环直到 EAGAIN(非阻塞 fd 无更多数据),完整行 = 一条消息
// ------------------------------------------------------------
void ZigbeeAdapter::poll_serial() {
  if (fd_ < 0) return;
  // ---- 读干净串口缓冲:循环 read 到 EAGAIN ----
  char buf[256];
  for (;;) {
    ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) {
      buffer_.append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break; // 非阻塞读没数据了:正常退出
    }
    // 真错误(如模块拔掉)或 EOF:退出,下轮再试
    if (n < 0) LOG_WARN("zigbee: read failed: %s", strerror(errno));
    break;
  }
  // ---- 按 '\n' 分帧:完整行 = 一条消息,残留尾巴留到下轮 ----
  size_t pos;
  while ((pos = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back(); // 兼容 CRLF
    if (line.empty()) continue; // 空行不是消息,跳过
    if (on_message) on_message("dev/mcu01/zigbee", line);
  }
}

} // namespace gateway
