#pragma once
// ============================================================
// 遥测持久化(sqlite3,单文件 amalgamation vendor 到 third_party/,同 mongoose 模式)
//
// 性能优化(用户提出:单片机可能高频单独上报各传感器字段):
//   高频遥测写入走「队列 + 写线程」——事件循环线程只把原始信封 push 进
//   内存队列(微秒级,零磁盘 IO),独立的写线程每 1s 或队列非空时批量取出,
//   在单个事务里批量 INSERT(ms 级磁盘 IO 全部丢给独立线程)。
//   这样单片机就算一秒几十条上报,服务端 HTTP/MQTT 响应也零延迟。
//
// 线程模型(唯一跨线程共享的是 pending_ 队列,互斥锁保护):
//   - enqueue_telemetry:事件循环线程调用,只 push,不碰磁盘
//   - writer_loop(独立线程):drain 队列 → 事务批量落库
//   - save_rule_state / query_history / load_rule_states:低频,直接访问 DB(带锁)
//   - 所有 sqlite 访问统一由 db_mutex_ 串行化(单连接多线程安全)
// ============================================================
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

struct sqlite3; // 前向声明(定义在 third_party/sqlite3.h)

namespace gateway
{
    class SqliteStore
    {
    public:
        // 打开数据库 + 建表(telemetry/rules_state)+ WAL + 启动写线程
        // 失败返回 false(此时所有方法安全空转)
        bool init(const std::string &db_path);

        // 高频:遥测信封入队(type=sensor 的原始 JSON,写线程里解析)
        // 非 sensor 信封(如 status 回执)在写线程里被过滤丢弃
        void enqueue_telemetry(const std::string &sensor_envelope);

        // 低频:规则启停状态持久化(直接写,带锁;每次 enable/disable 调一次)
        void save_rule_state(const std::string &rule_id, bool enabled);

        // 启动时:加载所有规则启停状态(重启后恢复用)
        void load_rule_states(std::map<std::string, bool> &out);

        // 查询最近 N 条遥测(时间升序),返回 JSON 数组(前端历史曲线用)
        // NULL 字段(单独上报时缺失)在 JSON 里省略该键
        std::string query_history(int limit);

        // 优雅关闭:flush 剩余队列 + 停写线程 + 关库
        void shutdown();

    private:
        void writer_loop();

        // db_ 是原子指针(M2 修复):enqueue_telemetry 在事件循环线程
        // 无锁判空(shutdown 会在写线程 join 之后把它置回 nullptr);
        // 其余所有真实 DB 操作仍统一在 db_mutex_ 下串行化。
        std::atomic<sqlite3 *> db_{nullptr};
        std::mutex db_mutex_;               // 保护所有 DB 访问(写线程 + 低频直接访问)
        std::mutex queue_mutex_;            // 保护 pending_ 队列
        std::condition_variable cv_;        // 唤醒写线程(有数据/停止)
        std::deque<std::string> pending_;   // 待写遥测队列(唯一跨线程共享;有上限,超限丢最旧)
        std::atomic<bool> stop_{false};     // 停止标志(原子,跨线程读写)
        std::thread writer_;
    };
} // namespace gateway
