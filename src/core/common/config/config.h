#pragma once
#include <string>

namespace gateway
{
    struct Config
    {
        int server_port = 8080;
        std::string mqtt_broker = "127.0.0.1:1883";
        std::string mqtt_topic_report = "dev/mcu01/report";
        std::string mqtt_topic_cmd = "dev/mcu01/cmd";
        std::string device_id = "mcu01";
        std::string log_level = "INFO";
    };
    // 从文件加载配置
    Config load_config(const std::string &path);
}
