#pragma once

// ============================================================
// 设备状态管理
//
// 6 个外设(温湿度一体出 2 个测量字段):
//   LED 灯        → 执行器 led_on(0/1) + led_br(0-100)
//   温湿度传感器  → 传感器 temp + humi
//   蜂鸣器        → 执行器 buzzer(0/1)
//   光敏传感器    → 传感器 light
//   红外传感器    → 传感器 ir
//   电机          → 执行器 motor_on + motor_sp + motor_dir
//
// 职责:
//   1. 接收 MQTT 上报(update_from_report),更新内存状态缓存
//   2. 生成 /api/status 聚合 JSON(get_status_json)
//   3. 维护设备注册表信息(在线状态 last_seen)
// 数据流:MQTT上报 → update_from_report → 内存缓存 → /api/status
// ============================================================
#include <string>

namespace gateway
{
    // 传感器测量值(上报字段:body.data.temp/humi/light/ir)
    struct SensorReading
    {
        std::string temp;  // 温度(温湿度传感器)
        std::string humi;  // 湿度(温湿度传感器)
        std::string light; // 光照(光敏传感器)
        std::string ir;    // 红外(红外传感器)
    };

    // 执行器状态(命令字段:body.led_on/.../buzzer)
    // 注意:这是网关侧缓存的"最后一次下发的状态",不是传感器读数
    struct ActuatorState
    {
        int led_on = 0;    // LED 开关(1=亮,0=灭)
        int led_br = 0;    // LED 亮度(PWM 0-100)
        int motor_on = 0;  // 电机开关(1=开,0=关)
        int motor_sp = 0;  // 电机速度(PWM 0-100)
        int motor_dir = 0; // 电机方向(0=正,1=反)
        int buzzer = 0;    // 蜂鸣器(1=响,0=停)
    };

    // 设备状态管理:接收 MQTT 上报,维护内存状态缓存
    class Device
    {
    public:
        // 处理一条 MQTT 上报信封,更新状态缓存
        // envelope 形如:
        //   {"type":"sensor","dev":"mcu01","ts":"...","body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":2500}}}
        //   {"type":"status","dev":"mcu01","ts":"...","body":{"items":[{"name":"led","state":"on","value":80}]}}
        void update_from_report(const std::string &envelope);

        // 处理一条控制命令信封,更新执行器状态缓存(网关侧立即反映)
        // 调用时机:/api/control 成功下发后,不必等单片机回执
        // envelope 形如:
        //   {"type":"cmd","dev":"mcu01","ts":"...","body":{"led_on":1,"led_br":80,"motor_on":0,"motor_sp":0,"motor_dir":0,"buzzer":0}}
        void update_from_control(const std::string &envelope);

        // 生成 /api/status 的聚合 JSON(11 字段)
        // 返回:{"temp":"25.5","humi":"60.0","light":"500","ir":"2500",
        //        "led_on":1,"led_br":80,"motor_on":1,"motor_sp":50,"motor_dir":0,"buzzer":0,
        //        "last_report":"2026-08-15 10:00:00"}(last_report 空串=从未上报)
        std::string get_status_json() const;

        // 设备在线状态(最近上报时间,空=从未上报)
        std::string last_seen() const { return last_seen_; }

        // 缓存的 LED 亮度(0-100)。网关下发 led_on 命令时附带此值,
        // 兼容"整帧解析"型 MCU 固件(只收到 led_on 没有 led_br 时会把
        // PWM 置 0,灯"开了但看不见")。0 = 从未设置过。
        int led_brightness() const { return actuators_.led_br; }

    private:
        SensorReading sensors_;   // 传感器测量值
        ActuatorState actuators_; // 执行器状态缓存
        std::string last_seen_;   // 最后上报时间(在线状态判断用)
    };
}
