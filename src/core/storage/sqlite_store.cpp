#include "core/storage/sqlite_store.h"
#include "core/common/json_util.h"   // json_escape(查询结果序列化)
#include "core/common/logger/logger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>     // query_history 的行缓冲(倒序收集后反转)
#include <mongoose.h> // mg_json_get_str/num(纯函数,线程安全,写线程里解析用)
#include <sqlite3.h>  // vendored amalgamation

namespace gateway
{
    // ------------------------------------------------------------
    // fmt_num:double → 字符串(%g 去多余 0,和 device.cpp 一致)
    // ------------------------------------------------------------
    static std::string fmt_num(double v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    // ------------------------------------------------------------
    // insert_telemetry:解析一条 sensor 信封并 INSERT(写线程里调用)
    // 单独上报场景:报文里缺哪个字段,对应列就绑 NULL(稀疏数据可接受)
    // 返回 false = 非 sensor 信封或无任何遥测字段(跳过)
    // ------------------------------------------------------------
    static bool insert_telemetry(sqlite3 *db, const std::string &envelope)
    {
        struct mg_str json = mg_str_n(envelope.data(), envelope.size());
        // 只收 sensor 信封(status 回执/命令不落遥测表)
        char *type = mg_json_get_str(json, "$.type");
        if (type == nullptr) return false;
        bool is_sensor = (std::strcmp(type, "sensor") == 0);
        free(type);
        if (!is_sensor) return false;

        char *ts = mg_json_get_str(json, "$.ts"); // 可能为空,最后 free
        double temp = 0, humi = 0, light = 0, ir = 0;
        bool ht = mg_json_get_num(json, "$.body.data.temp", &temp);
        bool hh = mg_json_get_num(json, "$.body.data.humi", &humi);
        bool hl = mg_json_get_num(json, "$.body.data.light", &light);
        bool hi = mg_json_get_num(json, "$.body.data.ir", &ir);
        if (!ht && !hh && !hl && !hi)
        {
            free(ts); // 没有任何遥测字段,不落库
            return false;
        }

        const char *sql =
            "INSERT INTO telemetry (ts,temp,humi,light,ir) VALUES (?,?,?,?,?)";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            free(ts);
            return false;
        }
        if (ts != nullptr) sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_text(stmt, 1, "", -1, SQLITE_TRANSIENT);
        if (ht) sqlite3_bind_double(stmt, 2, temp); else sqlite3_bind_null(stmt, 2);
        if (hh) sqlite3_bind_double(stmt, 3, humi); else sqlite3_bind_null(stmt, 3);
        if (hl) sqlite3_bind_double(stmt, 4, light); else sqlite3_bind_null(stmt, 4);
        if (hi) sqlite3_bind_double(stmt, 5, ir);    else sqlite3_bind_null(stmt, 5);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            // 磁盘满/IO 错误等:至少留条日志(L3 修复),否则遥测静默丢失
            LOG_WARN("storage: insert failed: %s", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
        free(ts);
        return true;
    }

    // ------------------------------------------------------------
    // init:打开数据库 + 建表 + WAL + 启动写线程
    // ------------------------------------------------------------
    bool SqliteStore::init(const std::string &db_path)
    {
        sqlite3 *raw = nullptr;
        if (sqlite3_open(db_path.c_str(), &raw) != SQLITE_OK)
        {
            LOG_ERROR("storage: open %s failed: %s", db_path.c_str(),
                      raw ? sqlite3_errmsg(raw) : "unknown");
            if (raw) { sqlite3_close(raw); }
            return false;
        }
        db_.store(raw); // 原子发布:此后写线程/enqueue 才可见
        // 建表(幂等):遥测历史 + 规则启停状态
        const char *t1 =
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "ts TEXT NOT NULL,"
            "temp REAL, humi REAL, light REAL, ir REAL)";
        const char *t2 =
            "CREATE TABLE IF NOT EXISTS rules_state ("
            "rule_id TEXT PRIMARY KEY, enabled INTEGER NOT NULL)";
        char *err = nullptr;
        sqlite3_exec(raw, t1, nullptr, nullptr, &err);
        if (err != nullptr)
        {
            LOG_WARN("storage: create telemetry failed: %s", err);
            sqlite3_free(err);
            err = nullptr;
        }
        sqlite3_exec(raw, t2, nullptr, nullptr, &err);
        if (err != nullptr)
        {
            LOG_WARN("storage: create rules_state failed: %s", err);
            sqlite3_free(err);
            err = nullptr;
        }
        // WAL:读(查询曲线)写(批量落库)互不阻塞
        sqlite3_exec(raw, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        LOG_INFO("storage: opened %s", db_path.c_str());

        stop_ = false;
        writer_ = std::thread(&SqliteStore::writer_loop, this);
        return true;
    }

    // ------------------------------------------------------------
    // writer_loop:写线程主循环
    //   每 1s 或被唤醒时 drain 队列 → 单事务批量 INSERT
    //   stop_ 置位后先 flush 剩余队列再退出(优雅关闭不丢数据)
    // ------------------------------------------------------------
    void SqliteStore::writer_loop()
    {
        sqlite3 *db = db_.load(); // 写线程启动时 db_ 已由 init 原子发布
        std::deque<std::string> batch;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lk(queue_mutex_);
                cv_.wait_for(lk, std::chrono::seconds(1), [this]
                             { return stop_.load() || !pending_.empty(); });
                if (pending_.empty() && stop_.load())
                {
                    break; // 队列清空且要求停止 → 退出
                }
                batch.swap(pending_); // 取走全部待写,释放锁
            }
            if (!batch.empty() && db != nullptr)
            {
                // 单事务批量写:一次 fsync 落多条,避免逐条 fsync 的慢
                std::lock_guard<std::mutex> dblk(db_mutex_);
                sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
                for (const std::string &env : batch)
                {
                    insert_telemetry(db, env);
                }
                char *err = nullptr;
                if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &err) != SQLITE_OK)
                {
                    // 磁盘满等:留日志,别静默丢批(L3 修复)
                    LOG_WARN("storage: batch commit failed: %s",
                             err != nullptr ? err : "unknown");
                    if (err != nullptr) sqlite3_free(err);
                }
                batch.clear();
            }
        }
    }

    // ------------------------------------------------------------
    // enqueue_telemetry:事件循环线程调用,只 push 队列(微秒级,零磁盘 IO)
    // ------------------------------------------------------------
    void SqliteStore::enqueue_telemetry(const std::string &sensor_envelope)
    {
        if (db_.load() == nullptr) return; // 未初始化成功,直接丢弃(遥测历史可丢)
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            pending_.push_back(sensor_envelope);
            // 队列上限(M2 修复):写线程因磁盘卡死停摆时,队列无限增长
            // 会撑爆内存。超限丢最旧(遥测可丢,系统活着更重要)。
            if (pending_.size() > 10000)
            {
                pending_.pop_front();
                LOG_WARN("storage: telemetry queue full, dropped oldest");
            }
        }
        cv_.notify_one();
    }

    // ------------------------------------------------------------
    // save_rule_state:规则启停状态落库(INSERT OR REPLACE 幂等)
    // ------------------------------------------------------------
    void SqliteStore::save_rule_state(const std::string &rule_id, bool enabled)
    {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3 *db = db_.load();
        if (db == nullptr) return;
        const char *sql =
            "INSERT OR REPLACE INTO rules_state (rule_id, enabled) VALUES (?,?)";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(stmt, 1, rule_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, enabled ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // ------------------------------------------------------------
    // load_rule_states:启动时加载所有规则启停状态(重启后恢复)
    // ------------------------------------------------------------
    void SqliteStore::load_rule_states(std::map<std::string, bool> &out)
    {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3 *db = db_.load();
        if (db == nullptr) return;
        const char *sql = "SELECT rule_id, enabled FROM rules_state";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            std::string id((const char *)sqlite3_column_text(stmt, 0));
            bool en = sqlite3_column_int(stmt, 1) != 0;
            out[id] = en;
        }
        sqlite3_finalize(stmt);
    }

    // ------------------------------------------------------------
    // query_history:查询最近 N 条遥测,时间升序返回 JSON 数组
    //   [{"ts":"...","temp":25.5,"humi":60},...](缺失字段省略键)
    // ------------------------------------------------------------
    std::string SqliteStore::query_history(int limit)
    {
        if (limit <= 0) limit = 100;
        if (limit > 1000) limit = 1000; // 上限防呆,别一次拉爆内存
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3 *db = db_.load();
        if (db == nullptr) return "[]";

        const char *sql =
            "SELECT ts,temp,humi,light,ir FROM telemetry ORDER BY id DESC LIMIT ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return "[]";
        sqlite3_bind_int(stmt, 1, limit);

        std::vector<std::string> rows; // 倒序收集(最新在前),末尾反转成升序
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            std::string row = "{\"ts\":\"";
            const unsigned char *ts = sqlite3_column_text(stmt, 0);
            row += json_escape(ts ? (const char *)ts : "");
            row += "\"";
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
            {
                row += ",\"temp\":";
                row += fmt_num(sqlite3_column_double(stmt, 1));
            }
            if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
            {
                row += ",\"humi\":";
                row += fmt_num(sqlite3_column_double(stmt, 2));
            }
            if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            {
                row += ",\"light\":";
                row += fmt_num(sqlite3_column_double(stmt, 3));
            }
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            {
                row += ",\"ir\":";
                row += fmt_num(sqlite3_column_double(stmt, 4));
            }
            row += "}";
            rows.push_back(row);
        }
        sqlite3_finalize(stmt);

        std::string out = "[";
        for (int i = (int)rows.size() - 1; i >= 0; i--) // 反转:最旧在前
        {
            if (i != (int)rows.size() - 1) out += ",";
            out += rows[i];
        }
        out += "]";
        return out;
    }

    // ------------------------------------------------------------
    // shutdown:优雅关闭(主循环永不退出,此方法为将来优雅退出预留)
    // 顺序:置停止位 → 唤醒写线程 → join(等它 flush 完)→ 关库
    // ------------------------------------------------------------
    void SqliteStore::shutdown()
    {
        stop_ = true;
        cv_.notify_all();
        if (writer_.joinable())
        {
            writer_.join();
        }
        sqlite3 *db = db_.exchange(nullptr); // 先摘除再关库,杜绝任何并发访问
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        LOG_INFO("storage: closed");
    }
} // namespace gateway
