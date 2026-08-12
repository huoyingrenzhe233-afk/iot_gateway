#include "core/common/config/config.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>

namespace gateway
{

    // ------------------------------------------------------------
    // load_config:从 YAML 文件加载配置到 Config 结构体
    //
    // 对应 config/gateway.yaml:
    //   server.port        → cfg.server_port       (HTTP 监听端口)
    //   mqtt.broker        → cfg.mqtt_broker       (broker 地址,含协议前缀)
    //   mqtt.topic_report  → cfg.mqtt_topic_report (订阅:上报 topic)
    //   mqtt.topic_cmd     → cfg.mqtt_topic_cmd    (发布:命令 topic)
    //   device.id          → cfg.device_id         (设备标识,组信封用)
    //   log.level          → cfg.log_level         (日志级别)
    //
    // 容错:文件不存在/解析失败时,打印告警并返回默认值(Config 结构体
    // 的成员默认值),程序照常启动 —— 不会因为配置问题崩溃
    // ------------------------------------------------------------
    Config load_config(const std::string &path)
    {
        Config cfg; // 先取默认值,下面按文件内容逐个覆盖
        try
        {
            // yaml-cpp 加载整个文件成节点树
            YAML::Node root = YAML::LoadFile(path);

            // 每个 if 判断"这个键存在吗",存在才覆盖默认值
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
            // 任何解析异常都兜住:告警 + 用默认值继续
            std::printf("[WARN] load config %s failed: %s, using defaults\n",
                        path.c_str(), e.what());
        }
        return cfg;
    }

} // namespace gateway