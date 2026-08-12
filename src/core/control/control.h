#pragma once
#include <string>

namespace gateway {

class Control {
public:
  // 输入:HTTP body,如 {"type":"control","payload":{"led_on":1,...}}
  // 输出:MQTT 信封 {"type":"cmd","dev":"mcu01","ts":"...","body":{...}}
  // 失败返回空字符串
  std::string build_control_envelope(const std::string &body,
                                     const std::string &device_id);
};
} // namespace gateway