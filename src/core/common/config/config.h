#pragma once
#include <string>

namespace gateway
{
    // ------------------------------------------------------------
    // 网关配置结构体:load_config() 从 config/gateway.yaml 填充
    // 成员默认值与 gateway.yaml 内容一致,文件缺失时用默认值
    // ------------------------------------------------------------
    struct Config
    {
        int server_port = 8080;                    // HTTP 监听端口
        std::string mqtt_broker = "127.0.0.1:1883"; // MQTT broker 地址
        std::string mqtt_topic_report = "dev/mcu01/report"; // 订阅:单片机上报
        std::string mqtt_topic_cmd = "dev/mcu01/cmd";       // 发布:控制命令
        std::string device_id = "mcu01";           // 设备标识(组信封 dev 字段)
        std::string log_level = "INFO";            // 日志级别
    };
    // 从文件加载配置(见 config.cpp,解析失败返回默认值)
    Config load_config(const std::string &path);
}
