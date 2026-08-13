#include "core/device/device_registry.h"
#include "core/common/json_util.h"   // json_escape(与 rule_engine/main.cpp 共用)
#include "core/common/logger/logger.h"

#include <cstdio>
#include <yaml-cpp/yaml.h>

namespace gateway
{
    // ------------------------------------------------------------
    // load:加载 sensors.yaml + actuators.yaml 到登记表
    // 两个文件结构相同:
    //   sensors:    - id: temp_1 / kind: sensor / protocol: mqtt / description: ...
    //   actuators:  - id: led_1 / kind: actuator / ...
    // ------------------------------------------------------------
    bool DeviceRegistry::load(const std::string &sensors_path,
                              const std::string &actuators_path)
    {
        bool ok = true;

        // 每个文件一个辅助 lambda:解析 "顶层键" 下的条目列表
        auto load_file = [this](const std::string &path,
                                const char *top_key) -> bool {
            try
            {
                YAML::Node root = YAML::LoadFile(path);
                YAML::Node list = root[top_key];
                if (!list || !list.IsSequence())
                {
                    LOG_WARN("registry: %s missing '%s' list", path.c_str(),
                             top_key);
                    return false;
                }
                for (auto item : list)
                {
                    DeviceEntry e;
                    e.id = item["id"].as<std::string>("");
                    e.kind = item["kind"].as<std::string>("");
                    e.protocol = item["protocol"].as<std::string>("mqtt");
                    e.description = item["description"].as<std::string>("");
                    // ⑤ kind 校验:只接受 sensor/actuator,非法值跳过
                    if (e.kind != "sensor" && e.kind != "actuator")
                    {
                        LOG_WARN("registry: %s entry '%s' has invalid kind '%s', skipped",
                                 path.c_str(), e.id.c_str(), e.kind.c_str());
                        continue;
                    }
                    if (!e.id.empty())
                    {
                        entries_.push_back(e);
                        LOG_INFO("registry: registered %s (%s)", e.id.c_str(),
                                 e.kind.c_str());
                    }
                }
                return true;
            }
            catch (const std::exception &ex)
            {
                LOG_WARN("registry: load %s failed: %s", path.c_str(),
                         ex.what());
                return false;
            }
        };

        // 先加载传感器,再加载执行器(保持登记顺序稳定)
        ok = load_file(sensors_path, "sensors") && ok;
        ok = load_file(actuators_path, "actuators") && ok;
        return ok;
    }

    // ------------------------------------------------------------
    // contains:判断某 id 是否已登记(规则引擎加载校验的权威来源)
    // kind 传 "sensor"/"actuator" 精确匹配,空串 = 任意类型
    // 找到第一个匹配即返回 true(登记表条目很少,线性扫描足够)
    // ------------------------------------------------------------
    bool DeviceRegistry::contains(const std::string &id,
                                  const std::string &kind) const
    {
        for (const DeviceEntry &e : entries_)
        {
            if (e.id == id && (kind.empty() || e.kind == kind))
            {
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------
    // find:按 id 查找登记条目,找不到返回 nullptr
    // (登记表条目少,线性扫描即可;返回指针指向 entries_ 内部元素,生命周期随本对象)
    // ------------------------------------------------------------
    const DeviceEntry *DeviceRegistry::find(const std::string &id) const
    {
        for (const DeviceEntry &e : entries_)
        {
            if (e.id == id)
            {
                return &e;
            }
        }
        return nullptr;
    }

    // ------------------------------------------------------------
    // to_json_detail:生成单条设备详情 JSON(老师验收 3.2.5#2)
    // last_seen 非空 = 收到过 MQTT 上报(在线),空 = 从未上报(离线)
    // 单台 mcu01 场景:所有外设共用同一个 last_seen(Device 缓存只有一份)
    // 找不到 id 返回空字符串(调用方据此回 404)
    // ------------------------------------------------------------
    std::string DeviceRegistry::to_json_detail(const std::string &id,
                                               const std::string &last_seen) const
    {
        const DeviceEntry *e = find(id);
        if (e == nullptr)
        {
            return "";
        }
        std::string out;
        out += "{\"id\":\"";
        out += json_escape(e->id);
        out += "\",\"kind\":\"";
        out += json_escape(e->kind);
        out += "\",\"protocol\":\"";
        out += json_escape(e->protocol);
        out += "\",\"description\":\"";
        out += json_escape(e->description);
        out += "\",\"online\":";
        out += last_seen.empty() ? "false" : "true";
        out += ",\"last_seen\":\"";
        out += json_escape(last_seen);
        out += "\"}";
        return out;
    }

    // ------------------------------------------------------------
    // to_json_list:生成 /api/devices 的 JSON 数组(手拼,不引第三方库)
    // 所有字符串字段过 json_escape,防特殊字符破坏 JSON
    // ------------------------------------------------------------
    std::string DeviceRegistry::to_json_list() const
    {
        std::string out;
        out.reserve(256 * entries_.size());
        out += "[";
        for (size_t i = 0; i < entries_.size(); i++)
        {
            const DeviceEntry &e = entries_[i];
            if (i > 0) out += ",";
            out += "{\"id\":\"";
            out += json_escape(e.id);
            out += "\",\"kind\":\"";
            out += json_escape(e.kind);
            out += "\",\"protocol\":\"";
            out += json_escape(e.protocol);
            out += "\",\"description\":\"";
            out += json_escape(e.description);
            out += "\"}";
        }
        out += "]";
        return out;
    }
}
