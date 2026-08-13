#include "core/rules/rule_engine.h"
#include "core/common/json_util.h"   // json_escape(与 device_registry/main.cpp 共用)
#include "core/common/logger/logger.h"
#include "core/control/control.h"
#include "core/device/device_registry.h"

#include <cstdio>
#include <map>
#include <mongoose.h>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace gateway {

    // ============================================================
    // id → 上报 JSON 路径映射表(共 4 条:4 个传感器 id)
    //
    // 为什么需要映射:规则 yaml 里写的是业务 id(如 temp_1),而传感器
    // 上报信封的 body 结构是 {"type":"sensor","dev":"mcu01","ts":"...",
    // "body":{"data":{"temp":32.5,"humi":60,"light":320,"ir":2500}}}。
    // 评估时要用 mongoose 的 JSONPath 表达式去取数,所以先把 id 翻译成路径。
    //
    // 约束:sensor_id 必须同时出现在本映射表 + 设备注册表,
    // 否则加载时直接拒掉该规则(防止 id 拼错导致"规则永不触发"的哑弹)。
    // ============================================================
    static const std::map<std::string, std::string> &sensor_paths()
    {
        static const std::map<std::string, std::string> m = {
            {"temp_1",  "$.body.data.temp"},  // 温度(温湿度传感器)
            {"humi_1",  "$.body.data.humi"},  // 湿度(温湿度传感器)
            {"light_1", "$.body.data.light"}, // 光照(光敏传感器)
            {"ir_1",    "$.body.data.ir"},    // 红外(红外传感器)
        };
        return m;
    }

    // ============================================================
    // 执行器 id → 允许下发字段白名单映射表(共 3 条:3 个执行器 id)
    //
    // 为什么需要白名单:单片机的命令信封只认固定的字段名(见 device.cpp
    // update_from_control 的 6 字段),规则里 field 必须命中白名单,
    // 否则单片机收到不认识的字段会静默忽略(又是哑弹)。
    // 执行器能下发的字段,与单片机固件实现一一对应:
    //   led_1    → led_on(开关)/led_br(亮度)
    //   motor_1  → motor_on(启停)/motor_sp(速度)/motor_dir(方向)
    //   buzzer_1 → buzzer(开关)
    // ============================================================
    static const std::map<std::string, std::vector<std::string>> &actuator_fields()
    {
        static const std::map<std::string, std::vector<std::string>> m = {
            {"led_1",    {"led_on", "led_br"}},
            {"motor_1",  {"motor_on", "motor_sp", "motor_dir"}},
            {"buzzer_1", {"buzzer"}},
        };
        return m;
    }

    // ------------------------------------------------------------
    // set_device_id:设置命令信封的 dev 字段
    // (main.cpp 启动时用 Config.device_id 调用,默认 "mcu01")
    // ------------------------------------------------------------
    void RuleEngine::set_device_id(const std::string &device_id)
    {
        device_id_ = device_id;
    }

    // ------------------------------------------------------------
    // parse:解析 rules.yaml 到 out(不触碰成员状态)
    //
    // 文件级失败(文件缺失 / 缺 "rules" 列表 / YAML 解析异常)→ false
    // 单条规则校验失败 → LOG_WARN + continue 跳过(文件仍算解析成功)
    // 校验规则(防"规则永不触发"的哑弹):
    //   1. id:非空 + 不重复(API 引用主键,重复会导致 set_enabled 歧义)
    //   2. sensor_id:必须在本文件传感器映射表里 + 注册表登记过
    //   3. op:必须是 ">"/"<"/">="/"<="/"==" 之一
    //   4. actuator_id:必须在本文件执行器白名单里 + 注册表登记过
    //   5. field:必须在对应执行器的白名单里
    // ------------------------------------------------------------
    bool RuleEngine::parse(const std::string &rules_path,
                           const DeviceRegistry &registry,
                           std::vector<Rule> &out)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(rules_path);
            YAML::Node list = root["rules"];
            if (!list || !list.IsSequence())
            {
                LOG_WARN("rules: %s missing 'rules' list", rules_path.c_str());
                return false;
            }

            for (auto item : list)
            {
                Rule r;
                r.id = item["id"].as<std::string>("");
                r.name = item["name"].as<std::string>("");
                r.enabled = item["enabled"].as<bool>(true); // 缺省 = 启用
                r.sensor_id = item["when"]["sensor"].as<std::string>("");
                r.op = item["when"]["op"].as<std::string>("");
                r.threshold = item["when"]["value"].as<double>(0);
                r.actuator_id = item["then"]["actuator"].as<std::string>("");
                r.field = item["then"]["field"].as<std::string>("");
                r.value = item["then"]["value"].as<long>(0);

                // ① id:非空(空 id 无法被 API 引用)
                if (r.id.empty())
                {
                    LOG_WARN("rules: entry with empty id, skipped");
                    continue;
                }
                // ② id:不重复(逐条比对已解析的规则)
                bool dup = false;
                for (const Rule &prev : out)
                {
                    if (prev.id == r.id)
                    {
                        dup = true;
                        break;
                    }
                }
                if (dup)
                {
                    LOG_WARN("rules: duplicate rule id '%s', skipped",
                             r.id.c_str());
                    continue;
                }
                // ③ sensor_id:映射表 + 注册表双重校验
                const auto &sp = sensor_paths();
                if (sp.find(r.sensor_id) == sp.end() ||
                    !registry.contains(r.sensor_id, "sensor"))
                {
                    LOG_WARN("rules: rule '%s' unknown sensor_id '%s', skipped",
                             r.id.c_str(), r.sensor_id.c_str());
                    continue;
                }
                // ④ op:白名单比较符
                if (r.op != ">" && r.op != "<" && r.op != ">=" &&
                    r.op != "<=" && r.op != "==")
                {
                    LOG_WARN("rules: rule '%s' invalid op '%s', skipped",
                             r.id.c_str(), r.op.c_str());
                    continue;
                }
                // ⑤ actuator_id:白名单 + 注册表双重校验
                const auto &af = actuator_fields();
                auto it = af.find(r.actuator_id);
                if (it == af.end() || !registry.contains(r.actuator_id, "actuator"))
                {
                    LOG_WARN("rules: rule '%s' unknown actuator_id '%s', skipped",
                             r.id.c_str(), r.actuator_id.c_str());
                    continue;
                }
                // ⑥ field:必须命中对应执行器的白名单
                bool field_ok = false;
                for (const std::string &f : it->second)
                {
                    if (f == r.field)
                    {
                        field_ok = true;
                        break;
                    }
                }
                if (!field_ok)
                {
                    LOG_WARN("rules: rule '%s' field '%s' not allowed for '%s', skipped",
                             r.id.c_str(), r.field.c_str(), r.actuator_id.c_str());
                    continue;
                }

                // name 缺省 = 取 id(前端展示兜底)
                if (r.name.empty())
                {
                    r.name = r.id;
                }

                out.push_back(r);
                LOG_INFO("rules: loaded rule '%s' (%s): when %s %s %.1f -> then %s.%s=%ld",
                         r.id.c_str(), r.name.c_str(), r.sensor_id.c_str(),
                         r.op.c_str(), r.threshold, r.actuator_id.c_str(),
                         r.field.c_str(), r.value);
            }
            return true;
        }
        catch (const std::exception &ex)
        {
            LOG_WARN("rules: load %s failed: %s", rules_path.c_str(), ex.what());
            return false;
        }
    }

    // ------------------------------------------------------------
    // load:首次加载规则
    // 先解析到临时 vector,成功才整体替换 rules_(失败保持原状)
    // ------------------------------------------------------------
    bool RuleEngine::load(const std::string &rules_path,
                          const DeviceRegistry &registry)
    {
        std::vector<Rule> fresh;
        if (!parse(rules_path, registry, fresh))
        {
            return false;
        }
        rules_ = fresh;
        LOG_INFO("rules: loaded %zu rules from %s", rules_.size(),
                 rules_path.c_str());
        return true;
    }

    // ------------------------------------------------------------
    // reload:热重载规则(运行时改配置,不用重启)
    //
    // 重载保留运行时启停状态:先把旧规则的 enabled 快照下来,新规则里
    // 同 id 的规则沿用旧 enabled;新增规则用 yaml 里的默认值(true)。
    // parse 失败 → 保留旧规则,不允许出现"半更新"状态。
    // ------------------------------------------------------------
    bool RuleEngine::reload(const std::string &rules_path,
                            const DeviceRegistry &registry)
    {
        std::map<std::string, bool> old_enabled;
        for (const Rule &r : rules_)
        {
            old_enabled[r.id] = r.enabled;
        }

        std::vector<Rule> fresh;
        if (!parse(rules_path, registry, fresh))
        {
            LOG_WARN("rules: reload failed, keeping old rules");
            return false;
        }
        // 重载保留运行时启停状态
        for (Rule &r : fresh)
        {
            auto it = old_enabled.find(r.id);
            if (it != old_enabled.end())
            {
                r.enabled = it->second;
            }
        }
        rules_ = fresh;
        LOG_INFO("rules: reloaded %zu rules from %s", rules_.size(),
                 rules_path.c_str());
        return true;
    }

    // ------------------------------------------------------------
    // evaluate:收到一条上报信封时评估所有规则
    //
    // 只处理 type == "sensor" 的信封(传感器数值在 body.data 里);
    // status 回执 / 其它类型不含传感器测量值,直接忽略。
    //
    // 上升沿(rising edge)触发语义:
    //   只有"上次不满足 → 这次满足"的瞬间才 fire 一次,之后持续满足
    //   不再重复下发。为什么?单片机每 2 秒上报一次,若每次满足都 fire,
    //   高温时蜂鸣器命令会以 2 秒一次的频率刷爆 MQTT。恢复规则
    //   (如 temp_alarm_off)负责在条件回退时把动作关掉。
    // ------------------------------------------------------------
    void RuleEngine::evaluate(const std::string &envelope)
    {
        // 1. 取 type 字段(用 mg_json_get_str 去引号,别用 get_tok 会带引号)
        struct mg_str json = mg_str_n(envelope.data(), envelope.size());
        char *type = mg_json_get_str(json, "$.type");
        if (type == nullptr)
        {
            return; // 没有 type 字段,不是合法信封
        }
        std::string t(type);
        free(type); // mg_json_get_str 返回 malloc 内存,必须 free,否则泄漏
        if (t != "sensor")
        {
            return; // 非 sensor 上报不带传感器数值,不参与评估
        }

        // 2. 逐条规则评估
        for (Rule &r : rules_) // 非 const 引用:要写 last_satisfied
        {
            if (!r.enabled)
            {
                continue; // 已停用规则直接跳过
            }
            const auto &sp = sensor_paths();
            auto it = sp.find(r.sensor_id);
            if (it == sp.end())
            {
                continue; // 防御:映射缺失(parse 已保证,理论上到不了)
            }
            double v = 0;
            if (!mg_json_get_num(json, it->second.c_str(), &v))
            {
                continue; // 本次上报没带这个字段(如没上报 humi)→ 跳过
            }

            bool satisfied = compare(v, r.op, r.threshold);
            // 上升沿触发:只有"不满足 → 满足"跨越瞬间才下发命令
            if (satisfied && !r.last_satisfied)
            {
                LOG_INFO("rule '%s' triggered: %s %s %.1f", r.id.c_str(),
                         r.sensor_id.c_str(), r.op.c_str(), r.threshold);
                fire(r);
            }
            r.last_satisfied = satisfied; // 记住本次结果,供下次判断沿
        }
    }

    // ------------------------------------------------------------
    // compare:数值比较(parse 已保证 op 合法,这里兜底防御)
    // ------------------------------------------------------------
    bool RuleEngine::compare(double actual, const std::string &op,
                             double threshold)
    {
        if (op == ">")  return actual > threshold;
        if (op == "<")  return actual < threshold;
        if (op == ">=") return actual >= threshold;
        if (op == "<=") return actual <= threshold;
        if (op == "==") return actual == threshold;
        LOG_WARN("rules: unknown op '%s'", op.c_str());
        return false;
    }

    // ------------------------------------------------------------
    // fire:组控制命令信封并回调 on_action(不下发=哑弹,所以必须回调)
    // 信封格式复用 Control::build_field_envelope(协议格式单一来源):
    //   {"type":"cmd","dev":"mcu01","ts":"2026-08-13 10:00:00","body":{"buzzer":1}}
    // 只下发 rule.field 这一个字段(规则引擎只负责单一动作)。
    // ------------------------------------------------------------
    void RuleEngine::fire(const Rule &rule)
    {
        std::string envelope =
            Control::build_field_envelope(device_id_, rule.field, rule.value);
        // 动作回调:main.cpp 里赋值 = 发布到 MQTT + 同步状态缓存
        if (on_action)
        {
            on_action(envelope);
        }
    }

    // ------------------------------------------------------------
    // set_enabled:启停一条规则(/api/rules/:id/enable|disable 调用)
    // 找到返回 true;找不到 LOG_WARN + 返回 false(API 回 404)
    // ------------------------------------------------------------
    bool RuleEngine::set_enabled(const std::string &id, bool enabled)
    {
        for (Rule &r : rules_)
        {
            if (r.id == id)
            {
                r.enabled = enabled;
                LOG_INFO("rules: rule '%s' %s", id.c_str(),
                         enabled ? "enabled" : "disabled");
                return true;
            }
        }
        LOG_WARN("rules: rule '%s' not found", id.c_str());
        return false;
    }

    // ------------------------------------------------------------
    // to_json_list:生成 /api/rules 的 JSON 数组(手拼,不引第三方库)
    // 每条:{"id","name","enabled","when":{sensor,op,value},"then":{actuator,field,value}}
    // 阈值是 double:用 %g 格式化(snprintf 到小缓冲,自动去掉多余 0)
    // 所有字符串字段过 json_escape,防特殊字符破坏 JSON
    // ------------------------------------------------------------
    std::string RuleEngine::to_json_list() const
    {
        std::string out;
        out.reserve(256 * rules_.size());
        out += "[";
        for (size_t i = 0; i < rules_.size(); i++)
        {
            const Rule &r = rules_[i];
            if (i > 0)
            {
                out += ",";
            }
            char numbuf[32];
            std::snprintf(numbuf, sizeof(numbuf), "%g", r.threshold);
            out += "{\"id\":\"";
            out += json_escape(r.id);
            out += "\",\"name\":\"";
            out += json_escape(r.name);
            out += "\",\"enabled\":";
            out += r.enabled ? "true" : "false";
            out += ",\"when\":{\"sensor\":\"";
            out += json_escape(r.sensor_id);
            out += "\",\"op\":\"";
            out += json_escape(r.op);
            out += "\",\"value\":";
            out += numbuf;
            out += "},\"then\":{\"actuator\":\"";
            out += json_escape(r.actuator_id);
            out += "\",\"field\":\"";
            out += json_escape(r.field);
            out += "\",\"value\":";
            out += std::to_string(r.value);
            out += "}}";
        }
        out += "]";
        return out;
    }
} // namespace gateway
