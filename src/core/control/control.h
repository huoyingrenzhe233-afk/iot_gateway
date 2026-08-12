#pragma once
#include <string>

namespace gateway
{

  // ------------------------------------------------------------
  // Control:控制命令处理工具
  // 把 Web/Qt 的 HTTP 控制请求,组装成发往单片机的 MQTT 命令信封
  // (阶段三核心:/api/control → dev/mcu01/cmd)
  // ------------------------------------------------------------
  class Control
  {
  public:
    // 输入:HTTP body,如 {"type":"control","body":{"led_on":1,...}}
    // 输出:MQTT 信封 {"type":"cmd","dev":"mcu01","ts":"...","body":{...}}
    // 失败返回空字符串
    std::string build_control_envelope(const std::string &body,
                                       const std::string &device_id);
  };
} // namespace gateway