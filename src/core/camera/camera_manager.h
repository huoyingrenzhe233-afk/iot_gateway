#pragma once
// ============================================================
// 摄像头管理(阶段摄像头接口,方案 C:全链路 MJPEG,零转码)
// 职责:
//   1. start_stream/stop_stream:fork/kill mjpg_streamer(推流,摄像头原生 MJPEG)
//   2. snapshot:fork wget 从 mjpg-streamer ?action=snapshot 抓一帧存 jpg
//   3. start_record/stop_record:fork/kill ffmpeg -c copy 录像(不编码,纯拷贝)
// 线程模型:所有方法在 mongoose 事件循环线程调用;子进程用 fork+exec 异步
// 启动,不阻塞事件循环(snapshot 除外,waitpid 阻塞 ~0.2s 可接受)
// 关键决策:不用 h264_rkmpp/HLS(RK3568 MPP 栈不稳定 + 延迟大),走老师
// plan.md 的 mjpg-streamer 参考路线(已验证路径)
// ============================================================
#include <string>
#include <sys/types.h>   // pid_t

namespace gateway
{
    class CameraManager
    {
    public:
        // 配置摄像头设备节点和 mjpg-streamer 端口(来自 config/gateway.yaml)
        void set_config(const std::string &device, int port);
        // 启动推流:fork mjpg_streamer;已在跑则返回 false
        bool start_stream();
        // 停止推流:kill + waitpid
        bool stop_stream();
        // 开始录像:fork ffmpeg 从 mjpg-streamer 拉流 -c copy
        bool start_record();
        // 停止录像
        bool stop_record();
        // 抓拍:fork wget 抓一帧存 snapshots/snapshot_<ts>.jpg;成功回填 filename(纯文件名)
        bool snapshot(std::string &filename);
        // 状态查询(推流/录像是否在跑)
        bool stream_running() const;
        bool record_running() const;

    private:
        pid_t stream_pid_ = -1;     // mjpg_streamer 进程号(-1=未启动)
        pid_t record_pid_ = -1;     // ffmpeg 录像进程号
        std::string device_ = "/dev/video9"; // 摄像头 V4L2 节点(可配置)
        int port_ = 8080;                   // mjpg-streamer HTTP 端口
    };
} // namespace gateway
