#include "core/device/device_registry.h"
#include "core/common/logger/logger.h"

#include <cstdio>
#include <yaml-cpp/yaml.h>

namespace gateway
{
    // ------------------------------------------------------------
    // json_escape:JSON 字符串转义(防 description 里的引号/反斜杠破坏 JSON)
    // 只处理常见危险字符:" \ \n \r \t 和控制字符
    // ------------------------------------------------------------
    static std::string json_escape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += c;
                }
            }
        }
        return out;
    }

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
