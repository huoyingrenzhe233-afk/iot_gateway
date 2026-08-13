// ============================================================
// 摄像头管理实现(方案 C:mjpg-streamer 推流 + wget 抓拍 + ffmpeg 录像)
// 进程模型:
//   - 所有子进程都用 fork()+execvp() 启动(绝不用 system()/popen(),
//     避免 shell 注入和阻塞事件循环)
//   - 子进程关闭继承的 fd、stdout/stderr 重定向到 /dev/null
//   - 子进程退出后的僵尸由 main.cpp 的 reap_children 定时器收割
//   - 全部方法在 mongoose 事件循环线程调用,无需加锁
// ============================================================
#include "core/camera/camera_manager.h"
#include "core/common/logger/logger.h" // LOG_INFO/LOG_WARN/LOG_ERROR
#include <cstdio>
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
  // timestamp:当前时间戳,格式 "%Y%m%d_%H%M%S"
  //   例:20260813_101530(抓拍/录像文件名用,保证不重名)
  // 用 localtime_r + strftime(非线程安全的 gmtime 不用)
  // ------------------------------------------------------------
  static std::string timestamp()
  {
    char buf[32];
    std::time_t now = std::time(nullptr);
    struct tm tm = {};
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return std::string(buf);
  }

  // ------------------------------------------------------------
  // spawn:fork + execvp 启动一个外部程序
  //   argv = 程序名 + 参数列表(不含 shell 解析,整串参数原样传给 execvp)
  //   子进程:
  //     1. 关闭 [3, sysconf(_SC_OPEN_MAX)) 所有继承的 fd
  //        (否则网关的监听 socket/日志文件会被子进程长期占着)
  //     2. stdout/stderr 重定向到 /dev/null(mjpg_streamer/ffmpeg 不刷屏)
  //     3. execvp 执行;失败则 _exit(127)(标准"命令未找到"退出码)
  //   父进程:直接返回子进程 pid(fork 失败返回 -1)
  // ------------------------------------------------------------
  static pid_t spawn(const std::vector<std::string> &argv)
  {
    pid_t pid = fork();
    if (pid < 0)
    {
      // fork 失败(进程数到上限等)
      return -1;
    }
    if (pid == 0)
    {
      // ---- 子进程 ----
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
      // execvp 要 char** 数组,std::vector<std::string> 不能直接传,
      // 先转成临时 char* 数组(结尾补 nullptr)
      std::vector<char *> cargv;
      cargv.reserve(argv.size() + 1);
      for (size_t i = 0; i < argv.size(); i++)
      {
        cargv.push_back(const_cast<char *>(argv[i].c_str()));
      }
      cargv.push_back(nullptr);
      execvp(argv[0].c_str(), cargv.data());
      _exit(127); // execvp 失败才会走到这里(命令不存在等)
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
  // 已在跑(stream_pid_ > 0)则直接返回 false,不重复 fork
  // ------------------------------------------------------------
  bool CameraManager::start_stream()
  {
    if (stream_pid_ > 0)
    {
      return false; // 已经在推流,别重复拉第二个 mjpg_streamer
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
  // stop_stream:停止推流(SIGTERM + waitpid)
  // SIGTERM 让 mjpg_streamer 优雅退出;waitpid 等它真正退完,
  // 避免留下僵尸进程(僵尸由 reap_children 兜底,这里直接收掉更干净)
  // ------------------------------------------------------------
  bool CameraManager::stop_stream()
  {
    if (stream_pid_ <= 0)
    {
      return false; // 没在跑,没什么可停的
    }
    kill(stream_pid_, SIGTERM);       // 发 SIGTERM 请求退出
    waitpid(stream_pid_, NULL, 0);    // 阻塞等它退出
    LOG_INFO("camera stream stopped: pid=%d", (int)stream_pid_);
    stream_pid_ = -1;
    return true;
  }

  // ------------------------------------------------------------
  // start_record:开始录像(fork ffmpeg 从 mjpg-streamer 拉流)
  // 纯拷贝方案:ffmpeg 从 http://127.0.0.1:port/?action=stream
  // 拉 MJPEG 流,参数 -c copy 直接复制进容器,零转码零编码
  // ------------------------------------------------------------
  bool CameraManager::start_record()
  {
    if (record_pid_ > 0)
    {
      return false; // 已经在录像
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
    kill(record_pid_, SIGTERM);
    waitpid(record_pid_, NULL, 0);
    LOG_INFO("camera record stopped: pid=%d", (int)record_pid_);
    record_pid_ = -1;
    return true;
  }

  // ------------------------------------------------------------
  // snapshot:抓拍一帧(fork wget 抓 ?action=snapshot 单张 JPEG)
  // 这是唯一阻塞方法:waitpid 等 wget 退出(抓一帧 ~0.2s,可接受)
  // 成功:filename 回填纯文件名(不带目录,前端拼 URL 用)
  // ------------------------------------------------------------
  bool CameraManager::snapshot(std::string &filename)
  {
    // 抓拍输出目录 snapshots/,不存在就建
    mkdir("snapshots", 0755);
    // 纯文件名:snapshot_20260813_101530.jpg(返回给调用方)
    filename = "snapshot_" + timestamp() + ".jpg";
    std::string path = "snapshots/" + filename;
    // wget -q -O <path> http://127.0.0.1:8080/?action=snapshot
    std::vector<std::string> argv;
    argv.push_back("wget");
    argv.push_back("-q"); // 安静模式(出错信息已重定向到 /dev/null)
    argv.push_back("-O");
    argv.push_back(path);
    argv.push_back("http://127.0.0.1:" + std::to_string(port_) + "/?action=snapshot");
    pid_t pid = spawn(argv);
    if (pid <= 0)
    {
      LOG_ERROR("camera snapshot FAILED: fork/exec wget failed");
      return false;
    }
    // 阻塞等 wget 退出(快:一帧 JPEG 就几 KB)
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
    {
      // wget 退出码 0 = 抓到并写盘成功
      LOG_INFO("camera snapshot ok: %s", path.c_str());
      return true;
    }
    // 退出码非 0 = 抓取失败(最常见原因:mjpg_streamer 还没启动)
    LOG_WARN("camera snapshot FAILED: wget exit=%d (stream not running?)",
             WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return false;
  }

  // ------------------------------------------------------------
  // stream_running/record_running:进程是否还活着
  // kill(pid, 0) 只探测进程存在、不发任何信号
  // 注意:僵尸进程也会返回 0(进程存在但已退出),演示场景够用
  // ------------------------------------------------------------
  bool CameraManager::stream_running() const
  {
    return stream_pid_ > 0 && kill(stream_pid_, 0) == 0;
  }

  bool CameraManager::record_running() const
  {
    return record_pid_ > 0 && kill(record_pid_, 0) == 0;
  }

} // namespace gateway
