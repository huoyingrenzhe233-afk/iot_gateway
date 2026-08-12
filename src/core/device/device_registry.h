#pragma once

// ============================================================
// 设备注册表(极简版,阶段二)
//
// 只有一台单片机 mcu01,不做动态注册/自动发现(UpsertMqttDeviceFromTopic
// 等复杂机制砍掉),改为从 config/devices/*.yaml 静态登记 6 个外设。
//
// 职责:
//   1. 启动时加载 sensors.yaml + actuators.yaml(登记表)
//   2. GET /api/devices 返回登记清单(老师验收:至少 3 传感器+3 执行器)
//   3. 为规则引擎提供 sensor_id/actuator_id 的权威来源
//
// 数据流:config/devices/*.yaml → load → 内存 vector → /api/devices
// ============================================================
#include <string>
#include <vector>

namespace gateway
{
    // 单条设备登记(对应 yaml 里一条记录)
    struct DeviceEntry
    {
        std::string id;          // 设备 id,如 temp_1(规则引用的权威 id)
        std::string kind;        // "sensor" | "actuator"
        std::string protocol;    // "mqtt"
        std::string description; // 中文说明(便于前端展示)
    };

    // 设备注册表:静态登记,不做动态注册
    class DeviceRegistry
    {
    public:
        // 从 config/devices/sensors.yaml + actuators.yaml 加载登记表
        // 失败(文件缺失)时返回 false,但不清空已加载的条目
        bool load(const std::string &sensors_path,
                  const std::string &actuators_path);

        // 登记表条目数(调试/测试用)
        size_t size() const { return entries_.size(); }

        // 生成 /api/devices 的 JSON 数组
        // 返回:[{"id":"temp_1","kind":"sensor","protocol":"mqtt","description":"温度(温湿度传感器)"},...]
        std::string to_json_list() const;

    private:
        std::vector<DeviceEntry> entries_; // 全部登记(传感器+执行器)
    };
}
