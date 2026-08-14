// ============================================================
// 摄像头管理实现(mjpg-streamer 推流 + wget 抓拍 + ffmpeg 录像)
// 进程模型:
//   - 所有子进程都用 fork()+execv() 启动(绝不用 system()/popen(),
//     避免 shell 注入和阻塞事件循环)
//   - 子进程关闭继承的 fd、stdout/stderr 重定向到 /dev/null
//   - 子进程退出后的僵尸由 main.cpp 的 reap_children 定时器收割
//   - 全部方法在 mongoose 事件循环线程调用,无需加锁
// ============================================================
#include "core/camera/camera_manager.h"
#include "core/common/logger/logger.h" // LOG_INFO/LOG_WARN/LOG_ERROR
#include <cerrno>     // errno(ECHILD 等,waitpid 非阻塞探测用)
#include <chrono>     // 毫秒时间戳(timestamp 用)
#include <cstdio>
#include <cstdlib>    // getenv(resolve_executable 的 PATH 查找)
#include <cstring>
#include <ctime>
#include <fcntl.h>     // open/O_WRONLY(子进程重定向 /dev/null)
#include <sys/stat.h>  // mkdir
#include <sys/types.h> // pid_t
#include <sys/wait.h>  // waitpid/WIFEXITED/WEXITSTATUS
#include <unistd.h>    // fork/execvp/kill/sysconf/dup2/close
#include <vector>

namespace gateway
{

  // ------------------------------------------------------------
  // timestamp:当前时间戳,格式 "%Y%m%d_%H%M%S_%03d"(带毫秒)
  //   例:20260813_101530_123(抓拍/录像文件名用;毫秒级保证同一秒内
  //   双端并发抓拍/录像不会同名覆盖)
  // 用 localtime_r + strftime(非线程安全的 gmtime 不用)
  // ------------------------------------------------------------
  static std::string timestamp()
  {
    char buf[40];
    std::time_t now = std::time(nullptr);
    struct tm tm = {};
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()) %
              1000;
    char out[48];
    std::snprintf(out, sizeof(out), "%s_%03d", buf, static_cast<int>(ms.count()));
    return std::string(out);
  }

  // ------------------------------------------------------------
  // resolve_executable:在父进程(fork 前)把命令名解析成绝对路径
  //   带 '/' 直接用;否则按 PATH 逐目录找可执行文件。
  //   为什么在父进程做:fork 后的子进程里 execvp 内部可能 malloc
  //   (glibc 实现),多线程进程 fork 后有 malloc 死锁风险(见 spawn 注释)。
  //   解析不成功返回原名字,execv 失败后子进程 _exit(127),与 execvp 行为一致。
  // ------------------------------------------------------------
  static std::string resolve_executable(const std::string &name)
  {
    if (name.find('/') != std::string::npos)
    {
      return name; // 已带路径,直接用
    }
    const char *path_env = getenv("PATH");
    if (path_env == nullptr)
    {
      return name;
    }
    std::string path_list(path_env);
    size_t start = 0;
    while (start <= path_list.size())
    {
      size_t end = path_list.find(':', start);
      std::string dir = (end == std::string::npos)
                            ? path_list.substr(start)
                            : path_list.substr(start, end - start);
      if (dir.empty())
      {
        dir = ".";
      }
      std::string full = dir + "/" + name;
      if (access(full.c_str(), X_OK) == 0)
      {
        return full;
      }
      if (end == std::string::npos)
      {
        break;
      }
      start = end + 1;
    }
    return name; // 没找到:保持原名字,execv 失败后 _exit(127)
  }

  // ------------------------------------------------------------
  // spawn:fork + execv 启动一个外部程序
  //   argv = 程序名 + 参数列表(不含 shell 解析,整串参数原样传给 execv)
  //   线程安全要点(M1 修复):argv→char* 数组转换、PATH 解析、可执行文件
  //   定位全部在 fork 之前的父进程完成;子进程在 exec 前只做
  //   close/open/dup2/execv 这些 async-signal-safe 调用,绝不 malloc。
  //   原因:进程里常驻 SQLite 写线程,fork 只复制调用线程;若 fork 瞬间
  //   另一线程正持有 malloc 锁,子进程里任何 malloc 都会永久死锁。
  //   子进程:
  //     1. 关闭 [3, sysconf(_SC_OPEN_MAX)) 所有继承的 fd
  //        (否则网关的监听 socket/日志文件会被子进程长期占着)
  //     2. stdout/stderr 重定向到 /dev/null(mjpg_streamer/ffmpeg 不刷屏)
  //     3. execv 执行;失败则 _exit(127)(标准"命令未找到"退出码)
  //   父进程:直接返回子进程 pid(fork 失败返回 -1)
  // ------------------------------------------------------------
  static pid_t spawn(const std::vector<std::string> &argv)
  {
    // ---- fork 前(父进程,多线程安全):准备 exec 用的绝对路径和 char* 数组 ----
    std::string exe = resolve_executable(argv[0]);
    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (size_t i = 0; i < argv.size(); i++)
    {
      cargv.push_back(const_cast<char *>(argv[i].c_str()));
    }
    cargv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0)
    {
      // fork 失败(进程数到上限等)
      return -1;
    }
    if (pid == 0)
    {
      // ---- 子进程:只做 async-signal-safe 调用 ----
      // 关掉所有继承的 fd:exec 前必须做,否则子进程永远占着
      // 网关的 socket 和日志文件,父进程无法复用它们
      long maxfd = sysconf(_SC_OPEN_MAX);
      for (int fd = 3; fd < maxfd; fd++)
      {
        close(fd); // 关闭失败(已关/无效)也直接忽略
      }
      // stdout/stderr → /dev/null:子进程是后台服务,日志我们不关心
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0)
      {
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
      execv(exe.c_str(), cargv.data());
      _exit(127); // execv 失败才会走到这里(命令不存在等)
    }
    // ---- 父进程:返回子进程 pid ----
    return pid;
  }

  // ------------------------------------------------------------
  // set_config:配置摄像头设备节点和 mjpg-streamer 端口
  // 由 main() 从 config/gateway.yaml 读出来传进来(默认 /dev/video9:8080)
  // ------------------------------------------------------------
  void CameraManager::set_config(const std::string &device, int port)
  {
    device_ = device;
    port_ = port;
  }

  // ------------------------------------------------------------
  // start_stream:启动推流(fork mjpg_streamer)
  // 已在跑则直接返回 false,不重复 fork(用 stream_running 真实探测,
  // 崩溃后的 stale pid 会被自动清理,允许重启)
  // ------------------------------------------------------------
  bool CameraManager::start_stream()
  {
    if (stream_running())
    {
      return false; // 已经在推流(真实探测,崩溃后的 stale pid 会被清理)
    }
    // mjpg_streamer 命令行(与 shell 手敲等价,但走 fork+execvp):
    //   mjpg_streamer -i "input_uvc.so -d /dev/video9 -r 640x480 -f 15 -q 85"
    //                 -o "output_http.so -p 8080"
    // ⚠️ -i/-o 各自是"一整串含空格的单个 argv",由 mjpg_streamer 内部
    //    再自行切分解析;这里绝不能按空格拆开(shell 写法不适用)
    std::vector<std::string> argv;
    argv.push_back("mjpg_streamer");
    argv.push_back("-i");
    argv.push_back("input_uvc.so -d " + device_ + " -r 640x480 -f 15 -q 85");
    argv.push_back("-o");
    argv.push_back("output_http.so -p " + std::to_string(port_));
    stream_pid_ = spawn(argv);
    if (stream_pid_ > 0)
    {
      LOG_INFO("camera stream started: pid=%d device=%s port=%d",
               (int)stream_pid_, device_.c_str(), port_);
    }
    else
    {
      LOG_ERROR("camera stream start FAILED: fork/exec mjpg_streamer failed");
    }
    return stream_pid_ > 0;
  }

  // ------------------------------------------------------------
  // stop_child:优雅停止一个子进程,不无限阻塞事件循环
  //   SIGTERM 请求退出 → 每 50ms 非阻塞探测(WNOHANG) → 超时 SIGKILL 强杀
  //   → 再限时 1s 收尸;仍收不到就放弃(交给 reap_children 兜底)。
  //   为什么:原 waitpid(pid,0) 若子进程卡死不响应 SIGTERM,事件循环冻结;
  //   且子进程若处于 D 状态(不可中断 IO),连 SIGKILL 都无法立即终止,
  //   无限阻塞 waitpid 会把整个网关冻死。所有等待都有上限。
  //   返回 true = 进程已停止或已交给兜底回收(pid 被置 -1,允许重启)
  // ------------------------------------------------------------
  static bool stop_child(pid_t &pid, int timeout_ms)
  {
    if (pid <= 0)
    {
      return false; // 没在跑,没什么可停的
    }
    kill(pid, SIGTERM); // 请求优雅退出
    int waited = 0;
    while (waited < timeout_ms)
    {
      int st = 0;
      pid_t r = waitpid(pid, &st, WNOHANG); // 非阻塞探测,不阻塞事件循环
      if (r == pid || (r < 0 && errno == ECHILD))
      {
        pid = -1; // 已退出(本次收割)或已被 reap_children 收割
        return true;
      }
      usleep(50 * 1000); // 50ms 后再探
      waited += 50;
    }
    // 超时未退出:SIGKILL 强杀(SIGKILL 不可忽略/阻塞,进程必死)
    kill(pid, SIGKILL);
    // 强杀后收尸也限时(1s):D 状态进程可能暂时杀不掉,继续无限等会冻死事件循环
    int waited2 = 0;
    while (waited2 < 1000)
    {
      int st = 0;
      pid_t r = waitpid(pid, &st, WNOHANG);
      if (r == pid || (r < 0 && errno == ECHILD))
      {
        break; // 已收尸(或已被 reap_children 收走)
      }
      usleep(50 * 1000);
      waited2 += 50;
    }
    pid = -1; // 极端情况(进程仍在 D 状态):放弃跟踪,僵尸由 reap_children 兜底
    return true;
  }

  // ------------------------------------------------------------
  // stop_stream:停止推流(SIGTERM 优雅退出,超时 SIGKILL 兜底)
  // 不再用阻塞 waitpid(pid,0),避免子进程卡死时冻结整个事件循环
  // ------------------------------------------------------------
  bool CameraManager::stop_stream()
  {
    if (stream_pid_ <= 0)
    {
      return false; // 没在跑,没什么可停的
    }
    pid_t old = stream_pid_;
    bool ok = stop_child(stream_pid_, 3000); // 最多等 3s
    LOG_INFO("camera stream stopped: pid=%d", (int)old);
    return ok;
  }

  // ------------------------------------------------------------
  // start_record:开始录像(fork ffmpeg 从 mjpg-streamer 拉流)
  // 纯拷贝方案:ffmpeg 从 http://127.0.0.1:port/?action=stream
  // 拉 MJPEG 流,参数 -c copy 直接复制进容器,零转码零编码
  // ------------------------------------------------------------
  bool CameraManager::start_record()
  {
    if (record_running())
    {
      return false; // 已经在录像
    }
    // 录像依赖推流:流没启动就 fork ffmpeg,ffmpeg 连不上会立刻退出,
    // 但 API 已返回 ok=true → 前端 recordStarted=true 与真实状态失步。
    // 这里前置校验,未推流直接拒绝(调用方回 500/明确错误)。
    if (!stream_running())
    {
      LOG_WARN("camera: start_record rejected (stream not running)");
      return false;
    }
    // 录像输出目录 records/,不存在就建(mkdir 已存在返回 EEXIST,
    // 忽略即可 —— 目录在不在都是成功)
    mkdir("records", 0755);
    // 文件名带时间戳,避免覆盖上一段录像:records/record_20260813_101530.avi
    std::string filename = "records/record_" + timestamp() + ".avi";
    // ffmpeg -y -i http://127.0.0.1:8080/?action=stream -c copy <file>
    std::vector<std::string> argv;
    argv.push_back("ffmpeg");
    argv.push_back("-y"); // 覆盖同名文件(时间戳一般不会重名,防御)
    argv.push_back("-i");
    argv.push_back("http://127.0.0.1:" + std::to_string(port_) + "/?action=stream");
    argv.push_back("-c");
    argv.push_back("copy"); // 纯拷贝:不编码,不转码
    argv.push_back(filename);
    record_pid_ = spawn(argv);
    if (record_pid_ > 0)
    {
      LOG_INFO("camera record started: pid=%d file=%s",
               (int)record_pid_, filename.c_str());
    }
    else
    {
      LOG_ERROR("camera record start FAILED: fork/exec ffmpeg failed");
    }
    return record_pid_ > 0;
  }

  // ------------------------------------------------------------
  // stop_record:停止录像(和 stop_stream 完全对称)
  // SIGTERM 让 ffmpeg 收尾写完 AVI 容器再退出
  // ------------------------------------------------------------
  bool CameraManager::stop_record()
  {
    if (record_pid_ <= 0)
    {
      return false; // 没在录像
    }
    pid_t old = record_pid_;
    bool ok = stop_child(record_pid_, 3000); // 最多等 3s,让 ffmpeg 写完 AVI 尾
    LOG_INFO("camera record stopped: pid=%d", (int)old);
    return ok;
  }

  // ------------------------------------------------------------
  // wait_exit_ok:限时等待子进程退出,返回"是否以 0 退出码正常退出"
  //   只等不杀(用 WNOHANG 轮询,永不无限阻塞);超时返回 false,
  //   僵尸留给 reap_children 兜底。
  // ------------------------------------------------------------
  static bool wait_exit_ok(pid_t pid, int timeout_ms)
  {
    int waited = 0;
    while (waited < timeout_ms)
    {
      int st = 0;
      pid_t r = waitpid(pid, &st, WNOHANG);
      if (r == pid)
      {
        return WIFEXITED(st) && WEXITSTATUS(st) == 0;
      }
      if (r < 0 && errno == ECHILD)
      {
        return false; // 已被 reap_children 收割,拿不到退出码,按失败处理
      }
      usleep(50 * 1000);
      waited += 50;
    }
    return false; // 超时按失败处理
  }

  // ------------------------------------------------------------
  // snapshot:抓拍一帧(fork wget 抓 ?action=snapshot 单张 JPEG)
  // 等待有硬上限(H1 修复):wget 加 -T 5 总超时 + 网关侧限时 7s 轮询。
  // 历史隐患:wget 默认读超时 900s,若 mjpg_streamer 进程活着但流卡住,
  // 一次抓拍会把整个事件循环冻死 15 分钟。现在最坏只等 7 秒。
  // 成功:filename 回填纯文件名(不带目录,前端拼 URL 用)
  // ------------------------------------------------------------
  bool CameraManager::snapshot(std::string &filename)
  {
    // 抓拍输出目录 snapshots/,不存在就建
    mkdir("snapshots", 0755);
    // 纯文件名:snapshot_20260813_101530_123.jpg(毫秒级,双端并发不覆盖)
    filename = "snapshot_" + timestamp() + ".jpg";
    std::string path = "snapshots/" + filename;
    // wget -q -T 5 -O <path> http://127.0.0.1:8080/?action=snapshot
    // -T 5:网络总超时 5 秒(busybox wget 与 GNU wget 均支持),防流卡住时挂 900s
    std::vector<std::string> argv;
    argv.push_back("wget");
    argv.push_back("-q"); // 安静模式(出错信息已重定向到 /dev/null)
    argv.push_back("-T");
    argv.push_back("5");  // 总超时 5 秒
    argv.push_back("-O");
    argv.push_back(path);
    argv.push_back("http://127.0.0.1:" + std::to_string(port_) + "/?action=snapshot");
    pid_t pid = spawn(argv);
    if (pid <= 0)
    {
      LOG_ERROR("camera snapshot FAILED: fork/exec wget failed");
      return false;
    }
    // 限时等待(wget 自身 5s 超时,这里给 7s 裕量):正常抓一帧 ~0.2s 返回
    if (wait_exit_ok(pid, 7000))
    {
      LOG_INFO("camera snapshot ok: %s", path.c_str());
      return true;
    }
    // 退出码非 0 / 超时 = 抓取失败(最常见原因:mjpg_streamer 还没启动或流卡住)
    LOG_WARN("camera snapshot FAILED: wget error or timeout (stream not running?)");
    return false;
  }

  // ------------------------------------------------------------
  // stream_running/record_running:进程是否真的还在运行
  // 用 waitpid(pid, &st, WNOHANG) 非阻塞探测(替代 kill(pid,0)):
  //   - 返回 0 = 还在运行(真 running)
  //   - 返回 pid/ECHILD = 已退出(收割掉 stale pid,返回 false)
  // 这样僵尸进程不再被误报为"运行中",崩溃后的 stale pid 也被自动清理
  // ------------------------------------------------------------
  bool CameraManager::stream_running()
  {
    if (stream_pid_ <= 0) return false;
    int st = 0;
    pid_t r = waitpid(stream_pid_, &st, WNOHANG);
    if (r == 0) return true;     // 还在运行
    if (r == stream_pid_)        // 已退出,本次收割到僵尸
    {
      stream_pid_ = -1;
      return false;
    }
    if (errno == ECHILD)         // 已被 reap_children 收割,pid 失效
    {
      stream_pid_ = -1;
      return false;
    }
    return true;                 // EINTR 等:保守认为还在跑
  }

  bool CameraManager::record_running()
  {
    if (record_pid_ <= 0) return false;
    int st = 0;
    pid_t r = waitpid(record_pid_, &st, WNOHANG);
    if (r == 0) return true;
    if (r == record_pid_)
    {
      record_pid_ = -1;
      return false;
    }
    if (errno == ECHILD)
    {
      record_pid_ = -1;
      return false;
    }
    return true;
  }

} // namespace gateway
