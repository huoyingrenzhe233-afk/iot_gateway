#include "core/device/device.h"
#include "core/common/logger/logger.h"

#include <cstdio>
#include <mongoose.h>

namespace gateway
{
    // ------------------------------------------------------------
    // update_from_report:解析一条 MQTT 上报信封,更新状态缓存
    //
    // 支持两种 type:
    //   sensor - body.data.{temp,humi,light,ir} 更新传感器测量值
    //   status - body.items[{name,state,value}] 更新执行器状态回执
    // 同时记录 last_seen(在线状态)
    // ------------------------------------------------------------
    void Device::update_from_report(const std::string &envelope)
    {
        struct mg_str json = mg_str_n(envelope.data(), envelope.size());

        // 1. 取 type 字段,区分 sensor 上报 vs status 回执
        //    注意:用 mg_json_get_str(去引号),不能用 mg_json_get_tok(含引号 "sensor")
        char *type = mg_json_get_str(json, "$.type");
        if (type == nullptr)
        {
            LOG_WARN("device: report missing type field");
            return;
        }
        std::string type_str(type);
        free(type);

        // 2. 记录最后上报时间(用 get_str 去引号,不能用 get_tok 会带引号)
        char *ts = mg_json_get_str(json, "$.ts");
        if (ts != nullptr)
        {
            last_seen_ = ts;
            free(ts);
        }

        // 3. 按类型更新
        if (type_str == "sensor")
        {
            // 传感器上报:body.data.{temp,humi,light,ir}
            // 注意:值是数字(25.6/60.1/320/2500),不能用 mg_json_get_str
            // (它只处理字符串值,数字会返回 null)。
            // 用 mg_json_get_num 取 double,再格式化成字符串存储。
            double v = 0;
            char buf[32];

            if (mg_json_get_num(json, "$.body.data.temp", &v))
            {
                std::snprintf(buf, sizeof(buf), "%g", v); // %g 自动去掉多余 0
                sensors_.temp = buf;
            }
            if (mg_json_get_num(json, "$.body.data.humi", &v))
            {
                std::snprintf(buf, sizeof(buf), "%g", v);
                sensors_.humi = buf;
            }
            if (mg_json_get_num(json, "$.body.data.light", &v))
            {
                std::snprintf(buf, sizeof(buf), "%g", v);
                sensors_.light = buf;
            }
            if (mg_json_get_num(json, "$.body.data.ir", &v))
            {
                std::snprintf(buf, sizeof(buf), "%g", v);
                sensors_.ir = buf;
            }
            LOG_INFO("device: sensor update temp=%s humi=%s light=%s ir=%s",
                     sensors_.temp.c_str(), sensors_.humi.c_str(),
                     sensors_.light.c_str(), sensors_.ir.c_str());
        }
        else if (type_str == "status")
        {
            // 执行器状态回执:body.items[] = {name, state, value}
            // 遍历 items 数组,按 name 匹配更新对应执行器状态
            // 终止条件:先试 mg_json_get_tok 判断 items[i].value 节点是否存在,
            // 节点不存在(tok.len==0)= 遍历完。不用 mg_json_get_long 的 -1
            // 默认值判断(会和合法值 -1 冲突,字符串值也会返回 -1)
            for (int i = 0; i < 16; i++) // 上限 16 项,防死循环
            {
                char path[64];
                std::snprintf(path, sizeof(path), "$.body.items[%d].value", i);
                struct mg_str tok = mg_json_get_tok(json, path);
                if (tok.len == 0) break; // 节点不存在 = 遍历完
                long val = mg_json_get_long(json, path, 0);

                // 取 name 字段(字符串,去引号)
                std::snprintf(path, sizeof(path), "$.body.items[%d].name", i);
                char *name = mg_json_get_str(json, path);
                std::string name_str = name ? name : "";
                free(name);

                // 按 name 映射到执行器字段(兼容短名/长名两套命名)
                if (name_str == "led" || name_str == "led_on")
                {
                    actuators_.led_on = (val != 0) ? 1 : 0;
                }
                else if (name_str == "led_br" || name_str == "br")
                {
                    actuators_.led_br = static_cast<int>(val);
                }
                else if (name_str == "motor" || name_str == "motor_on")
                {
                    actuators_.motor_on = (val != 0) ? 1 : 0;
                }
                else if (name_str == "motor_sp" || name_str == "sp")
                {
                    actuators_.motor_sp = static_cast<int>(val);
                }
                else if (name_str == "motor_dir" || name_str == "dir")
                {
                    actuators_.motor_dir = static_cast<int>(val);
                }
                else if (name_str == "buzzer")
                {
                    actuators_.buzzer = (val != 0) ? 1 : 0;
                }
                else
                {
                    LOG_WARN("device: status unknown item '%s'", name_str.c_str());
                }
            }
            LOG_INFO("device: status receipt led_on=%d led_br=%d motor_on=%d motor_sp=%d motor_dir=%d buzzer=%d",
                     actuators_.led_on, actuators_.led_br,
                     actuators_.motor_on, actuators_.motor_sp,
                     actuators_.motor_dir, actuators_.buzzer);
        }
        else
        {
            LOG_WARN("device: unknown report type '%s'", type_str.c_str());
        }
    }

    // ------------------------------------------------------------
    // update_from_control:处理控制命令信封,更新执行器状态缓存
    // /api/control 成功下发后调用,让 /api/status 立即反映新状态,
    // 不必等单片机回执(回执来了再覆盖,以实际执行为准)
    //
    // 命令信封是网关自己组包的(build_control_envelope),6 个字段必然
    // 齐全,所以直接取值赋值,不需要 -1 判断(那个判断和合法值有歧义)。
    // 取不到时 mg_json_get_long 返回默认值 0,不影响(理论上不会发生)。
    // ------------------------------------------------------------
    void Device::update_from_control(const std::string &envelope)
    {
        struct mg_str json = mg_str_n(envelope.data(), envelope.size());

        // 命令信封 body 里的 6 个字段(数值类型,直接取值赋值)
        actuators_.led_on =
            (mg_json_get_long(json, "$.body.led_on", 0) != 0) ? 1 : 0;
        actuators_.led_br =
            static_cast<int>(mg_json_get_long(json, "$.body.led_br", 0));
        actuators_.motor_on =
            (mg_json_get_long(json, "$.body.motor_on", 0) != 0) ? 1 : 0;
        actuators_.motor_sp =
            static_cast<int>(mg_json_get_long(json, "$.body.motor_sp", 0));
        actuators_.motor_dir =
            (mg_json_get_long(json, "$.body.motor_dir", 0) != 0) ? 1 : 0;
        actuators_.buzzer =
            (mg_json_get_long(json, "$.body.buzzer", 0) != 0) ? 1 : 0;

        LOG_INFO("device: control update led_on=%d led_br=%d motor_on=%d motor_sp=%d motor_dir=%d buzzer=%d",
                 actuators_.led_on, actuators_.led_br,
                 actuators_.motor_on, actuators_.motor_sp,
                 actuators_.motor_dir, actuators_.buzzer);
    }

    // ------------------------------------------------------------
    // get_status_json:生成 /api/status 聚合 JSON(老师规范 10 字段)
    // 传感器值用字符串(协议定稿),执行器状态用数值
    // ------------------------------------------------------------
    std::string Device::get_status_json() const
    {
        // 手拼 JSON(和 control.cpp 风格一致,不引第三方 JSON 库)
        std::string out;
        out.reserve(256);
        out += "{\"temp\":\"";
        out += sensors_.temp;
        out += "\",\"humi\":\"";
        out += sensors_.humi;
        out += "\",\"light\":\"";
        out += sensors_.light;
        out += "\",\"ir\":\"";
        out += sensors_.ir;
        out += "\",\"led_on\":";
        out += std::to_string(actuators_.led_on);
        out += ",\"led_br\":";
        out += std::to_string(actuators_.led_br);
        out += ",\"motor_on\":";
        out += std::to_string(actuators_.motor_on);
        out += ",\"motor_sp\":";
        out += std::to_string(actuators_.motor_sp);
        out += ",\"motor_dir\":";
        out += std::to_string(actuators_.motor_dir);
        out += ",\"buzzer\":";
        out += std::to_string(actuators_.buzzer);
        out += "}";
        return out;
    }
}
