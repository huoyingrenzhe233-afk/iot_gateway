#pragma once

// ============================================================
// 规则引擎(阶段七核心,老师验收 20 分)
// 职责:
//   1. 启动/重载时从 config/rules/rules.yaml 加载规则(校验 id 合法性)
//   2. 收到传感器上报(sensor 信封)时评估规则条件
//   3. 条件满足(上升沿触发)时组控制命令信封,回调 on_action 下发
//   4. 提供 /api/rules 系列 API 的数据(列表/重载/启停)
// 线程模型:仅在 mongoose 事件循环线程调用(单线程,无锁)
// ============================================================

#include <functional>
#include <string>
#include <vector>

namespace gateway {
    class DeviceRegistry; // 前向声明(加载校验用)

    // 单条规则(对应 rules.yaml 里一条记录)
    struct Rule {
        std::string id;          // 规则唯一 id(API 引用:/api/rules/:id/enable)
        std::string name;        // 中文名称(前端展示)
        bool enabled = true;     // 启停状态(运行时可变,reload 时保留)
        // when:传感器条件(单条件)
        std::string sensor_id;   // 传感器注册表 id(如 temp_1)
        std::string op;          // 比较符 ">"/"<"/">="/"<="/"=="
        double threshold = 0;    // 阈值
        // then:动作
        std::string actuator_id; // 执行器注册表 id(如 buzzer_1)
        std::string field;       // 下发命令字段(如 buzzer)
        long value = 0;          // 下发值
        bool last_satisfied = false; // 上次评估是否满足(仅上升沿触发,不持久化)
    };

    // 规则引擎:加载 → 评估 → 触发动作(发布命令信封)
    class RuleEngine {
    public:
        // 设置命令信封的 dev 字段(默认 "mcu01",与 Config.device_id 对齐)
        void set_device_id(const std::string &device_id);
        // 首次加载规则(parse 失败返回 false,成员状态不变)
        bool load(const std::string &rules_path, const DeviceRegistry &registry);
        // 热重载规则(保留旧规则的运行时启停状态;parse 失败保持旧规则)
        bool reload(const std::string &rules_path, const DeviceRegistry &registry);
        // 评估一条上报信封(仅 sensor 类型生效;上升沿触发)
        void evaluate(const std::string &envelope);
        // 启停某条规则(/api/rules/:id/enable|disable 用);找到返回 true
        bool set_enabled(const std::string &id, bool enabled);
        // 生成 /api/rules 的 JSON 数组(手拼,不引第三方库)
        std::string to_json_list() const;
        // 规则条数(调试/日志用)
        size_t size() const { return rules_.size(); }
        // 规则触发时的动作回调(由 main.cpp 赋值为:发布命令 + 同步状态缓存)
        std::function<void(const std::string &cmd_envelope)> on_action;
    private:
        // 解析 yaml 到 out(不触碰成员状态);成功 true,失败 false 且 out 可能部分填充(由调用方丢弃)
        bool parse(const std::string &rules_path, const DeviceRegistry &registry,
                   std::vector<Rule> &out);
        // 数值比较:op 只接受 ">"/"<"/">="/"<="/"=="(parse 已校验)
        static bool compare(double actual, const std::string &op, double threshold);
        // 组控制命令信封并回调 on_action(见 control.cpp 的同款格式)
        void fire(const Rule &rule);
        std::vector<Rule> rules_;        // 全部规则(解析成功后才整体替换)
        std::string device_id_ = "mcu01"; // 命令信封 dev 字段
    };
} // namespace gateway
