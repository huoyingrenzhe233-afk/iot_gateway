#include "core/common/config/config.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>

namespace gateway
{

    Config load_config(const std::string &path)
    {
        Config cfg;
        try
        {
            YAML::Node root = YAML::LoadFile(path);

            if (root["server"]["port"])
                cfg.server_port = root["server"]["port"].as<int>();

            if (root["mqtt"]["broker"])
                cfg.mqtt_broker = root["mqtt"]["broker"].as<std::string>();
            if (root["mqtt"]["topic_report"])
                cfg.mqtt_topic_report = root["mqtt"]["topic_report"].as<std::string>();
            if (root["mqtt"]["topic_cmd"])
                cfg.mqtt_topic_cmd = root["mqtt"]["topic_cmd"].as<std::string>();

            if (root["device"]["id"])
                cfg.device_id = root["device"]["id"].as<std::string>();

            if (root["log"]["level"])
                cfg.log_level = root["log"]["level"].as<std::string>();
        }
        catch (const std::exception &e)
        {
            std::printf("[WARN] load config %s failed: %s, using defaults\n",
                        path.c_str(), e.what());
        }
        return cfg;
    }

} // namespace gateway