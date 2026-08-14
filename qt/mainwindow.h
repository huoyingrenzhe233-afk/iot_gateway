#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// ============================================================
// Qt 端主窗口:与 web/index.html 功能完全一致的网关控制台
//   1. 每 1s 轮询 GET /api/status 刷新传感器 + 执行器状态
//   2. 控制:POST /api/control(全字段)+ 单开关
//   3. 摄像头:start/stop/snapshot/record + MJPEG 流解析显示
//   4. 通道切换:GET/POST /api/channel
//   5. 日志窗口(本地操作日志,上限 300 行)
// 网关地址:构造参数传入(板上同机 127.0.0.1,API 8081 / 视频 8080)
// ============================================================

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class QLabel;
class QPushButton;
class QCheckBox;
class QSlider;
class QTextEdit;
class QNetworkReply;
class QJsonObject;
class QFrame;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &gatewayIp, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void pollStatus();                       // 每秒轮询 /api/status
    void onStatusReply(QNetworkReply *r);    // 轮询响应
    void sendControl();                      // 全字段控制 POST /api/control
    void toggleChannel();                    // 切换 MQTT/ZigBee
    void onStartStream();                    // 摄像头:开始推流
    void onStopStream();                     // 摄像头:停止推流
    void onSnapshot();                       // 摄像头:抓拍
    void onToggleRecord();                   // 摄像头:录像开/停
    void onVideoReadyRead();                 // MJPEG 流数据到达
    void onVideoFinished();                  // MJPEG 流断开

private:
    void buildUi();                          // 构建界面(与 web 布局一致)
    void addLog(const QString &msg);         // 追加日志(上限 300 行)
    void applyStatus(const QJsonObject &s);  // 用 /api/status 刷新 UI
    void startVideoStream();                 // 发起 MJPEG 流请求
    void stopVideoStream();                  // 中断 MJPEG 流
    void finishStopStream();                 // 停流收尾(停视频/按钮状态)
    void httpGet(const QString &path, const std::function<void(QNetworkReply *)> &cb);
    void httpPost(const QString &path, const QJsonObject &body,
                  const std::function<void(QNetworkReply *)> &cb);
    void setChannelUi(const QString &transport); // 更新通道按钮文字
    void setDirUi(int dir);                      // 更新方向按钮高亮样式

    QString m_ip;                  // 网关 IP
    int m_apiPort = 8081;          // HTTP API 端口
    int m_videoPort = 8080;        // mjpg-streamer 端口

    QNetworkAccessManager m_http;  // HTTP 客户端
    QTimer m_pollTimer;            // 1s 轮询定时器

    // ---- UI(与 web 对应) ----
    QLabel *m_statusTag = nullptr;   // header 连接状态
    QPushButton *m_channelBtn = nullptr;
    QLabel *m_videoLabel = nullptr;  // MJPEG 画面
    QPushButton *m_startStreamBtn = nullptr;
    QPushButton *m_stopStreamBtn = nullptr;
    QPushButton *m_snapshotBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QLabel *m_tempLabel = nullptr, *m_humiLabel = nullptr;
    QLabel *m_lightLabel = nullptr, *m_irLabel = nullptr;
    QCheckBox *m_ledSw = nullptr;
    QSlider *m_ledBr = nullptr;
    QCheckBox *m_motorSw = nullptr;
    QSlider *m_motorSp = nullptr;
    QPushButton *m_dirFwdBtn = nullptr, *m_dirRevBtn = nullptr;
    QCheckBox *m_buzzerSw = nullptr;
    QTextEdit *m_logView = nullptr;

    // ---- 运行状态 ----
    bool m_streamStarted = false;    // 推流中
    bool m_recordStarted = false;    // 录像中
    bool m_polling = false;          // 轮询 in-flight 保护
    int m_currentDir = 0;            // 电机方向 0 正 / 1 反
    QString m_transport = "mqtt";    // 当前通道

    // ---- MJPEG 流 ----
    QNetworkReply *m_videoReply = nullptr;
    QByteArray m_videoBuffer;        // 未解析帧的残留字节
    QPixmap m_lastFrame;             // 最近一帧(resize 时重缩放)
};

#endif // MAINWINDOW_H
