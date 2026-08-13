#pragma once
// ============================================================
// JSON 工具(header-only,零依赖)
// json_escape:JSON 字符串转义(防引号/反斜杠/控制字符破坏 JSON)
// 三处共用:device_registry(设备列表)、rule_engine(规则列表)、
// main.cpp(WebSocket 广播),避免多份拷贝
// ============================================================
#include <cstdio>
#include <string>

namespace gateway
{
    // 只处理常见危险字符:" \ \n \r \t 和 <0x20 的控制字符
    inline std::string json_escape(const std::string &s)
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
} // namespace gateway
