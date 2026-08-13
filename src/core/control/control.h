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
    // 输入:HTTP body,如 {"type":"control","payload":{"led_on":1,...}}
    // 输出:MQTT 信封 {"type":"cmd","dev":"mcu01","ts":"...","body":{...}}
    // 失败返回空字符串
    std::string build_control_envelope(const std::string &body,
                                       const std::string &device_id);

    // 组单字段命令信封:{"type":"cmd","dev":"...","ts":"...","body":{field:value}}
    // 供 /api/actuators/:id/set 和规则引擎复用(与 build_control_envelope 区别:
    // 那个从 HTTP body 抠 payload,这个直接指定 field=value)
    static std::string build_field_envelope(const std::string &device_id,
                                            const std::string &field, long value);
  };
} // namespace gateway