// ============================================================
// 回归测试:get_status_json() 的 last_report 真实新鲜度字段
//
// 覆盖:
//   1. 从未上报 → last_report 保持空串 ""
//   2. sensor 上报后 last_report 反映 $.ts(update_from_report 写入)
//   3. ts 含引号/反斜杠 → JSON 转义(防破坏 JSON)
//   4. status 回执同样更新 last_report
//   5. 既有字段(传感器/执行器)不受影响,transport 仍由 main.cpp 附加
//
// 编译:cmake 目标 test_device_status(仅宿主机,见 CMakeLists.txt)
// ============================================================
#include "core/device/device.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define CHECK(cond, msg)                                    \
    do                                                      \
    {                                                       \
        if (cond)                                           \
        {                                                   \
            std::printf("PASS: %s\n", msg);                 \
        }                                                   \
        else                                                \
        {                                                   \
            std::printf("FAIL: %s\n", msg);                 \
            g_failures++;                                   \
        }                                                   \
    } while (0)

int main()
{
    gateway::Device dev;

    // 1. 从未上报 → last_report 为空串(不能是 null/缺字段)
    std::string json = dev.get_status_json();
    CHECK(json.find("\"last_report\":\"\"") != std::string::npos,
          "never reported -> last_report is empty string");
    CHECK(json.find("\"temp\":\"\"") != std::string::npos,
          "existing sensor fields unchanged (temp empty)");
    CHECK(json.find("\"led_on\":0") != std::string::npos,
          "existing actuator fields unchanged (led_on=0)");
    CHECK(json.find("\"transport\"") == std::string::npos,
          "transport is appended by main.cpp, not device json");

    // 2. sensor 上报:$.ts 写入 last_seen_ → last_report 反映
    dev.update_from_report(
        "{\"type\":\"sensor\",\"dev\":\"mcu01\",\"ts\":\"2026-08-15 10:00:00\","
        "\"body\":{\"data\":{\"temp\":25.6,\"humi\":60.1}}}");
    json = dev.get_status_json();
    CHECK(json.find("\"last_report\":\"2026-08-15 10:00:00\"") != std::string::npos,
          "sensor report ts appears in last_report");
    CHECK(json.find("\"temp\":\"25.6\"") != std::string::npos,
          "sensor temp updated alongside last_report");

    // 3. ts 含引号/反斜杠 → json_escape 转义,不破坏 JSON
    dev.update_from_report(
        "{\"type\":\"sensor\",\"dev\":\"mcu01\",\"ts\":\"a\\\"b\\\\c\","
        "\"body\":{\"data\":{\"temp\":1}}}");
    json = dev.get_status_json();
    CHECK(json.find("\"last_report\":\"a\\\"b\\\\c\"") != std::string::npos,
          "special chars in ts are JSON-escaped");

    // 4. status 回执同样刷新 last_report
    dev.update_from_report(
        "{\"type\":\"status\",\"dev\":\"mcu01\",\"ts\":\"2026-08-15 11:00:00\","
        "\"body\":{\"items\":[{\"name\":\"led\",\"state\":\"on\",\"value\":1}]}}");
    json = dev.get_status_json();
    CHECK(json.find("\"last_report\":\"2026-08-15 11:00:00\"") != std::string::npos,
          "status receipt updates last_report");
    CHECK(json.find("\"led_on\":1") != std::string::npos,
          "actuator state updated alongside last_report");

    // 5. 报告缺 ts → last_seen_ 保持不变(不覆盖为空)
    dev.update_from_report(
        "{\"type\":\"sensor\",\"dev\":\"mcu01\","
        "\"body\":{\"data\":{\"temp\":30.0}}}");
    json = dev.get_status_json();
    CHECK(json.find("\"last_report\":\"2026-08-15 11:00:00\"") != std::string::npos,
          "report without ts keeps previous last_report");

    if (g_failures > 0)
    {
        std::printf("RESULT: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("RESULT: ALL PASS\n");
    return 0;
}
