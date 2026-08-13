#include "core/common/logger/logger.h"
#include "core/control/control.h"
#include <ctime>
#include <mongoose.h>
#include <string>
namespace gateway
{

  // ------------------------------------------------------------
  // build_control_envelope:把 Web/Qt 发来的 /api/control 请求体,
  // 组装成发往单片机的 MQTT 命令信封
  //
  // 输入 body(HTTP 请求体):
  //   {"type":"control","payload":{"led_on":1,"led_br":80}}
  //   ⚠️ 键名是 payload(前端 index.html 定稿 + 老师 plan.md 原文,2026-08-12 对齐)
  // 输出(返回的 MQTT 信封):
  //   {"type":"cmd","dev":"mcu01","ts":"2026-08-12 10:00:00","body":{...}}
  // 失败(没有 payload 字段)返回空字符串
  // ------------------------------------------------------------
  static const char *kControlPayloadPath = "$.payload";
  std::string Control::build_control_envelope(const std::string &body,
                                              const std::string &device_id)
  {
    // 1. 用 mongoose 的 JSON 解析器,从请求体里抠出 kControlPayloadPath 这段
    //    (返回 mg_str 指向原始 body 内的子串,不拷贝)
    struct mg_str payload =
        mg_json_get_tok(mg_str_n(body.data(), body.size()), kControlPayloadPath);
    if (payload.len == 0)
    {
      // 请求体里没有 payload 字段 → 组包失败
      LOG_WARN("control:missing payload field");
      return "";
    }

    // 2. 生成时间戳 ts,格式 "YYYY-MM-DD HH:MM:SS"(协议定稿:字符串格式)
    char ts[32];
    std::time_t now = std::time(nullptr);
    struct tm tm = {};
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    // 3. 拼信封:type=cmd, dev=device_id, ts=时间戳, body=原样拷贝载荷
    std::string envelope;
    envelope.reserve(128 + payload.len); // 预分配,减少反复扩容
    envelope += "{\"type\":\"cmd\",\"dev\":\"";
    envelope += device_id;
    envelope += "\",\"ts\":\"";
    envelope += ts;
    envelope += "\",\"body\":";
    envelope.append(payload.buf, payload.len); // 原样带上载荷内容
    envelope += "}";

    LOG_INFO("control envelope: %s", envelope.c_str());
    return envelope;
  }

  // ------------------------------------------------------------
  // build_field_envelope:组"单字段"命令信封(协议格式的单一来源)
  // 供 /api/actuators/:id/set 和规则引擎复用,保证命令信封格式三处一致:
  //   {"type":"cmd","dev":"mcu01","ts":"2026-08-13 10:00:00","body":{"led_on":1}}
  // ------------------------------------------------------------
  std::string Control::build_field_envelope(const std::string &device_id,
                                            const std::string &field, long value)
  {
    char ts[32];
    std::time_t now = std::time(nullptr);
    struct tm tm = {};
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    std::string envelope;
    envelope.reserve(96);
    envelope += "{\"type\":\"cmd\",\"dev\":\"";
    envelope += device_id;
    envelope += "\",\"ts\":\"";
    envelope += ts;
    envelope += "\",\"body\":{\"";
    envelope += field;
    envelope += "\":";
    envelope += std::to_string(value);
    envelope += "}}";
    return envelope;
  }
} // namespace gateway