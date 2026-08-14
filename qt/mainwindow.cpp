#include "mainwindow.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>
#include <functional>

namespace {

// 时间戳 "HH:MM:SS"(web 日志同款)
QString nowTime()
{
    return QTime::currentTime().toString("HH:mm:ss");
}

} // namespace

MainWindow::MainWindow(const QString &gatewayIp, QWidget *parent)
    : QMainWindow(parent)
    , m_ip(gatewayIp)
{
    buildUi();
    addLog(QString("[系统] Qt 控制台启动,网关 %1:%2").arg(m_ip).arg(m_apiPort));

    // 启动时查询当前通道,同步按钮显示
    httpGet("/api/channel", [this](QNetworkReply *r) {
        if (r->error() == QNetworkReply::NoError)
        {
            QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            if (o.contains("transport"))
                setChannelUi(o["transport"].toString());
        }
    });

    // 每秒轮询状态(web setInterval 同款)
    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::pollStatus);
    m_pollTimer.start(1000);
    pollStatus();
    // VNC 平台下确保窗口激活可接收输入
    QTimer::singleShot(200, this, [this]() { activateWindow(); setFocus(); });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!m_lastFrame.isNull() && m_videoLabel)
    {
        m_videoLabel->setPixmap(m_lastFrame.scaled(m_videoLabel->size(),
                                                   Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    }
}

// ------------------------------------------------------------
// buildUi:构建与 web 一致的界面(header + 左侧视频/传感器 + 右侧控制/日志)
// ------------------------------------------------------------
void MainWindow::buildUi()
{
    setWindowTitle(QString("RK3568 智能网关控制系统 - %1").arg(m_ip));
    resize(1280, 800);

    // ---- 全局样式(模仿 web 浅色卡片风格) ----
    setStyleSheet(R"(
        QWidget#central { background: #f0f2f5; }
        QFrame#header { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 #4f46e5, stop:1 #7c3aed); border-radius: 16px; }
        QLabel#headerTitle { color: white; font-size: 20px; font-weight: 800;
            letter-spacing: 1px; padding-left: 8px; }
        QLabel#statusTag { color: white; background: rgba(255,255,255,0.2);
            border-radius: 12px; padding: 4px 14px; font-size: 13px; }
        QPushButton#channelBtn { color: white; background: rgba(255,255,255,0.15);
            border: 1px solid rgba(255,255,255,0.4); border-radius: 8px;
            padding: 6px 14px; font-size: 13px; }
        QFrame#card { background: white; border-radius: 16px; }
        QLabel#cardTitle { font-size: 17px; font-weight: 700; color: #1f2937; }
        QLabel#videoBox { background: #000; border-radius: 12px;
            border: 4px solid white; min-height: 280px; }
        QPushButton { border-radius: 8px; padding: 10px; font-weight: 600;
            font-size: 13px; color: white; }
        QPushButton#btnStart { background: #3b82f6; }
        QPushButton#btnStop { background: #64748b; }
        QPushButton#btnSnapshot { background: #10b981; }
        QPushButton#btnRecord { background: #ef4444; }
        QPushButton:disabled { background: #ccc; }
        QFrame#sCard { background: white; border-radius: 12px; }
        QLabel#sName { font-size: 13px; color: #333; }
        QLabel#sVal { font-size: 22px; font-weight: 800; color: #5c67f2; }
        QLabel#sUnit { font-size: 12px; color: #94a3b8; }
        QFrame#controlItem { background: #f8fafc; border-radius: 12px;
            border: 1px solid #edf2f7; }
        QLabel#ctrlName { font-weight: 600; font-size: 14px; }
        QPushButton#dirFwd, QPushButton#dirRev { background: #e2e8f0;
            color: #64748b; padding: 6px; font-size: 12px;
            border: 2px solid transparent; }
        QPushButton#dirFwd[active="true"], QPushButton#dirRev[active="true"] {
            background: #fff; color: #5c67f2; border-color: #5c67f2;
            font-weight: bold; }
        QTextEdit#logView { background: #0f172a; color: #38bdf8;
            border-radius: 12px; border: 1px solid #1e293b;
            font-family: 'Courier New'; font-size: 12px; }
    )");

    auto *central = new QWidget(this);
    central->setObjectName("central");
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(20);

    // ---- header ----
    auto *header = new QFrame(central);
    header->setObjectName("header");
    header->setFixedHeight(60);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(25, 0, 25, 0);
    auto *title = new QLabel("RK3568 智能网关控制系统", header);
    title->setObjectName("headerTitle");
    m_channelBtn = new QPushButton("通道:MQTT", header);
    m_channelBtn->setObjectName("channelBtn");
    m_statusTag = new QLabel("设备在线", header);
    m_statusTag->setObjectName("statusTag");
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_channelBtn);
    headerLayout->addWidget(m_statusTag);
    root->addWidget(header);

    // ---- 主区域 ----
    auto *mainRow = new QHBoxLayout();
    mainRow->setSpacing(20);
    root->addLayout(mainRow, 1);

    // ================= 左侧 =================
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(20);
    mainRow->addLayout(leftCol, 1);

    // 视频卡片
    auto *videoCard = new QFrame(central);
    videoCard->setObjectName("card");
    auto *videoLayout = new QVBoxLayout(videoCard);
    videoLayout->setContentsMargins(20, 20, 20, 20);
    videoLayout->setSpacing(15);
    auto *videoTitle = new QLabel("实时画面监控", videoCard);
    videoTitle->setObjectName("cardTitle");
    m_videoLabel = new QLabel(videoCard);
    m_videoLabel->setObjectName("videoBox");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setText("摄像头画面(点\"开始推流\"加载)");
    m_videoLabel->setStyleSheet(m_videoLabel->styleSheet() + "color:#888;");
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_startStreamBtn = new QPushButton("开始推流", videoCard);
    m_startStreamBtn->setObjectName("btnStart");
    m_stopStreamBtn = new QPushButton("停止推流", videoCard);
    m_stopStreamBtn->setObjectName("btnStop");
    m_stopStreamBtn->setEnabled(false);
    m_snapshotBtn = new QPushButton("抓拍照片", videoCard);
    m_snapshotBtn->setObjectName("btnSnapshot");
    m_recordBtn = new QPushButton("视频录制", videoCard);
    m_recordBtn->setObjectName("btnRecord");
    btnRow->addWidget(m_startStreamBtn);
    btnRow->addWidget(m_stopStreamBtn);
    btnRow->addWidget(m_snapshotBtn);
    btnRow->addWidget(m_recordBtn);
    videoLayout->addWidget(videoTitle);
    videoLayout->addWidget(m_videoLabel, 1);
    videoLayout->addLayout(btnRow);
    leftCol->addWidget(videoCard, 1);

    // 传感器卡片行
    auto *sensorRow = new QHBoxLayout();
    sensorRow->setSpacing(15);
    auto makeSensorCard = [&](const QString &name, const QString &unit,
                              QLabel **valLabel) {
        auto *card = new QFrame(central);
        card->setObjectName("sCard");
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(15, 15, 15, 15);
        l->setSpacing(2);
        auto *nameL = new QLabel(name, card);
        nameL->setObjectName("sName");
        nameL->setAlignment(Qt::AlignCenter);
        *valLabel = new QLabel("--", card);
        (*valLabel)->setObjectName("sVal");
        (*valLabel)->setAlignment(Qt::AlignCenter);
        auto *unitL = new QLabel(unit, card);
        unitL->setObjectName("sUnit");
        unitL->setAlignment(Qt::AlignCenter);
        l->addWidget(nameL);
        l->addWidget(*valLabel);
        l->addWidget(unitL);
        return card;
    };
    sensorRow->addWidget(makeSensorCard("温度", "°C", &m_tempLabel));
    sensorRow->addWidget(makeSensorCard("湿度", "% RH", &m_humiLabel));
    sensorRow->addWidget(makeSensorCard("光照强度", "Lux", &m_lightLabel));
    sensorRow->addWidget(makeSensorCard("红外检测", "Status", &m_irLabel));
    leftCol->addLayout(sensorRow);

    // ================= 右侧 =================
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(20);
    mainRow->addLayout(rightCol);

    // 控制卡片
    auto *ctrlCard = new QFrame(central);
    ctrlCard->setObjectName("card");
    ctrlCard->setFixedWidth(380);
    auto *ctrlLayout = new QVBoxLayout(ctrlCard);
    ctrlLayout->setContentsMargins(20, 20, 20, 20);
    ctrlLayout->setSpacing(12);
    auto *ctrlTitle = new QLabel("硬件外设控制", ctrlCard);
    ctrlTitle->setObjectName("cardTitle");
    ctrlLayout->addWidget(ctrlTitle);

    // LED
    auto *ledItem = new QFrame(ctrlCard);
    ledItem->setObjectName("controlItem");
    auto *ledLayout = new QVBoxLayout(ledItem);
    ledLayout->setContentsMargins(15, 15, 15, 15);
    auto *ledRow = new QHBoxLayout();
    auto *ledName = new QLabel("LED 照明灯", ledItem);
    ledName->setObjectName("ctrlName");
    m_ledSw = new QCheckBox(ledItem);
    ledRow->addWidget(ledName);
    ledRow->addStretch();
    ledRow->addWidget(m_ledSw);
    m_ledBr = new QSlider(Qt::Horizontal, ledItem);
    m_ledBr->setRange(0, 100);
    m_ledBr->setValue(50);
    ledLayout->addLayout(ledRow);
    ledLayout->addWidget(m_ledBr);
    ctrlLayout->addWidget(ledItem);

    // 电机
    auto *motorItem = new QFrame(ctrlCard);
    motorItem->setObjectName("controlItem");
    auto *motorLayout = new QVBoxLayout(motorItem);
    motorLayout->setContentsMargins(15, 15, 15, 15);
    auto *motorRow = new QHBoxLayout();
    auto *motorName = new QLabel("直流电机控制", motorItem);
    motorName->setObjectName("ctrlName");
    m_motorSw = new QCheckBox(motorItem);
    motorRow->addWidget(motorName);
    motorRow->addStretch();
    motorRow->addWidget(m_motorSw);
    m_motorSp = new QSlider(Qt::Horizontal, motorItem);
    m_motorSp->setRange(0, 100);
    m_motorSp->setValue(30);
    auto *dirRow = new QHBoxLayout();
    dirRow->setSpacing(8);
    m_dirFwdBtn = new QPushButton("正向旋转", motorItem);
    m_dirFwdBtn->setObjectName("dirFwd");
    m_dirFwdBtn->setProperty("active", true);
    m_dirRevBtn = new QPushButton("反向旋转", motorItem);
    m_dirRevBtn->setObjectName("dirRev");
    dirRow->addWidget(m_dirFwdBtn);
    dirRow->addWidget(m_dirRevBtn);
    motorLayout->addLayout(motorRow);
    motorLayout->addWidget(m_motorSp);
    motorLayout->addLayout(dirRow);
    ctrlLayout->addWidget(motorItem);

    // 蜂鸣器
    auto *buzzerItem = new QFrame(ctrlCard);
    buzzerItem->setObjectName("controlItem");
    buzzerItem->setStyleSheet(buzzerItem->styleSheet() +
                              "QFrame#controlItem{border-left:4px solid #f59e0b;}");
    auto *buzzerRow = new QHBoxLayout(buzzerItem);
    buzzerRow->setContentsMargins(15, 15, 15, 15);
    auto *buzzerName = new QLabel("紧急蜂鸣报警", buzzerItem);
    buzzerName->setObjectName("ctrlName");
    m_buzzerSw = new QCheckBox(buzzerItem);
    buzzerRow->addWidget(buzzerName);
    buzzerRow->addStretch();
    buzzerRow->addWidget(m_buzzerSw);
    ctrlLayout->addWidget(buzzerItem);
    ctrlLayout->addStretch();

    // 日志卡片
    auto *logCard = new QFrame(central);
    logCard->setObjectName("card");
    auto *logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(20, 20, 20, 20);
    auto *logTitle = new QLabel("系统运行日志", logCard);
    logTitle->setObjectName("cardTitle");
    m_logView = new QTextEdit(logCard);
    m_logView->setObjectName("logView");
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QTextEdit::NoWrap);
    logLayout->addWidget(logTitle);
    logLayout->addWidget(m_logView, 1);
    rightCol->addWidget(ctrlCard);
    rightCol->addWidget(logCard, 1);

    // ---- 信号绑定(web 事件绑定同款) ----
    connect(m_channelBtn, &QPushButton::clicked, this, &MainWindow::toggleChannel);
    connect(m_startStreamBtn, &QPushButton::clicked, this, &MainWindow::onStartStream);
    connect(m_stopStreamBtn, &QPushButton::clicked, this, &MainWindow::onStopStream);
    connect(m_snapshotBtn, &QPushButton::clicked, this, &MainWindow::onSnapshot);
    connect(m_recordBtn, &QPushButton::clicked, this, &MainWindow::onToggleRecord);
    connect(m_ledSw, &QCheckBox::clicked, this, &MainWindow::sendControl);
    // 用 sliderReleased 而非 valueChanged:轮询 setValue 会触发 valueChanged,
    // 会导致"轮询刷新 UI"误发控制命令(web 里 JS 赋值不触发 onchange)
    connect(m_ledBr, &QSlider::sliderReleased, this, &MainWindow::sendControl);
    connect(m_motorSw, &QCheckBox::clicked, this, &MainWindow::sendControl);
    connect(m_motorSp, &QSlider::sliderReleased, this, &MainWindow::sendControl);
    connect(m_buzzerSw, &QCheckBox::clicked, this, &MainWindow::sendControl);
    connect(m_dirFwdBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDir != 0)
        {
            m_currentDir = 0;
            setDirUi(0);
            addLog(QString("电机方向切换: 正转"));
            sendControl();
        }
    });
    connect(m_dirRevBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDir != 1)
        {
            m_currentDir = 1;
            setDirUi(1);
            addLog(QString("电机方向切换: 反转"));
            sendControl();
        }
    });
}

// ------------------------------------------------------------
// setDirUi:方向按钮高亮样式(选中蓝色边框,web .btn-dir.active 同款)
// ------------------------------------------------------------
void MainWindow::setDirUi(int dir)
{
    m_dirFwdBtn->setProperty("active", dir == 0);
    m_dirRevBtn->setProperty("active", dir == 1);
    m_dirFwdBtn->style()->unpolish(m_dirFwdBtn);
    m_dirFwdBtn->style()->polish(m_dirFwdBtn);
    m_dirRevBtn->style()->unpolish(m_dirRevBtn);
    m_dirRevBtn->style()->polish(m_dirRevBtn);
}

// ------------------------------------------------------------
// addLog:追加日志(上限 300 行,防内存无界增长)
// ------------------------------------------------------------
void MainWindow::addLog(const QString &msg)
{
    m_logView->append(QString("[%1] %2").arg(nowTime(), msg));
    // 超 300 行删最旧
    while (m_logView->document()->blockCount() > 300)
    {
        QTextCursor cur = m_logView->textCursor();
        cur.movePosition(QTextCursor::Start);
        cur.select(QTextCursor::LineUnderCursor);
        cur.removeSelectedText();
        cur.deleteChar(); // 删掉换行
    }
    // 自动滚动到底部
    QScrollBar *sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ------------------------------------------------------------
// HTTP 封装:GET/POST + JSON
// ------------------------------------------------------------
void MainWindow::httpGet(const QString &path,
                         const std::function<void(QNetworkReply *)> &cb)
{
    QNetworkRequest req(QUrl(QString("http://%1:%2%3").arg(m_ip).arg(m_apiPort).arg(path)));
    QNetworkReply *reply = m_http.get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cb]() {
                cb(reply);
                reply->deleteLater();
            });
}

void MainWindow::httpPost(const QString &path, const QJsonObject &body,
                          const std::function<void(QNetworkReply *)> &cb)
{
    QNetworkRequest req(QUrl(QString("http://%1:%2%3").arg(m_ip).arg(m_apiPort).arg(path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_http.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cb]() {
                cb(reply);
                reply->deleteLater();
            });
}

// ------------------------------------------------------------
// pollStatus:每秒轮询 /api/status(in-flight 保护防堆积)
// ------------------------------------------------------------
void MainWindow::pollStatus()
{
    if (m_polling)
        return; // 上一次还没返回,跳过本次
    m_polling = true;
    QNetworkRequest req(QUrl(QString("http://%1:%2/api/status").arg(m_ip).arg(m_apiPort)));
    QNetworkReply *reply = m_http.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onStatusReply(reply);
        m_polling = false;
        reply->deleteLater();
    });
}

void MainWindow::onStatusReply(QNetworkReply *r)
{
    if (r->error() != QNetworkReply::NoError)
    {
        // 连接断开(web catch 分支同款)
        m_statusTag->setText("连接断开");
        m_statusTag->setStyleSheet("color:#ff4d4d;background:rgba(255,255,255,0.2);");
        addLog(QString("传感器数据获取失败: %1").arg(r->errorString()));
        return;
    }
    QJsonObject s = QJsonDocument::fromJson(r->readAll()).object();
    if (s.isEmpty())
    {
        m_statusTag->setText("连接断开");
        m_statusTag->setStyleSheet("color:#ff4d4d;background:rgba(255,255,255,0.2);");
        return;
    }
    m_statusTag->setText("设备在线");
    m_statusTag->setStyleSheet("color:#fff;background:rgba(255,255,255,0.2);");
    applyStatus(s);
}

// ------------------------------------------------------------
// applyStatus:用 /api/status 刷新 UI(web setInterval 渲染同款)
// ------------------------------------------------------------
void MainWindow::applyStatus(const QJsonObject &s)
{
    // 传感器(字符串,空 = 未上报,显示 --)
    auto setVal = [](QLabel *label, const QJsonValue &v) {
        QString str = v.toString();
        label->setText(str.isEmpty() ? "--" : str);
    };
    setVal(m_tempLabel, s["temp"]);
    setVal(m_humiLabel, s["humi"]);
    setVal(m_lightLabel, s["light"]);

    // 红外:>2000 有人,文案与颜色统一(web M8 修复同款)
    int ir = s["ir"].toString().toInt();
    bool irDetected = ir > 2000;
    m_irLabel->setText(irDetected ? "有人" : "安全");
    m_irLabel->setStyleSheet(QString("font-size:22px;font-weight:800;color:%1;")
                                 .arg(irDetected ? "#ef4444" : "#10b981"));

    // 执行器状态(字段总存在,0 是合法值要保留;web ?? 50 语义)
    m_ledSw->setChecked(s["led_on"].toInt() == 1);
    m_ledBr->setValue(s.contains("led_br") ? s["led_br"].toInt() : 50);
    m_motorSw->setChecked(s["motor_on"].toInt() == 1);
    m_motorSp->setValue(s.contains("motor_sp") ? s["motor_sp"].toInt() : 30);
    if (s.contains("motor_dir"))
    {
        int dir = s["motor_dir"].toInt();
        if (dir != m_currentDir)
        {
            m_currentDir = dir;
            setDirUi(dir);
        }
    }
    m_buzzerSw->setChecked(s["buzzer"].toInt() == 1);
}

// ------------------------------------------------------------
// sendControl:全字段控制 POST /api/control(web cmd() 同款)
// ------------------------------------------------------------
void MainWindow::sendControl()
{
    QJsonObject payload;
    payload["led_on"] = m_ledSw->isChecked() ? 1 : 0;
    payload["led_br"] = m_ledBr->value();
    payload["motor_on"] = m_motorSw->isChecked() ? 1 : 0;
    payload["motor_sp"] = m_motorSp->value();
    payload["motor_dir"] = m_currentDir;
    payload["buzzer"] = m_buzzerSw->isChecked() ? 1 : 0;
    QJsonObject body;
    body["type"] = "control";
    body["payload"] = payload;

    addLog(QString("发送指令: ") +
           QString::fromUtf8(QJsonDocument(body).toJson()));
    httpPost("/api/control", body, [this](QNetworkReply *r) {
        // 失败也要提示(web M4 修复同款:不静默)
        if (r->error() != QNetworkReply::NoError)
        {
            int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray data = r->readAll();
            QJsonObject err = QJsonDocument::fromJson(data).object();
            QString msg = err["message"].toString();
            addLog(QString("指令发送失败: %1 %2")
                       .arg(code)
                       .arg(msg.isEmpty() ? r->errorString() : msg));
        }
    });
}

// ------------------------------------------------------------
// toggleChannel:切换 MQTT/ZigBee 通道
// ------------------------------------------------------------
void MainWindow::toggleChannel()
{
    QString target = (m_transport == "mqtt") ? "zigbee" : "mqtt";
    QJsonObject body;
    body["transport"] = target;
    addLog(QString("正在切换到 %1 通道...").arg(target == "zigbee" ? "ZigBee" : "MQTT"));
    httpPost("/api/channel/switch", body, [this](QNetworkReply *r) {
        QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o["ok"].toBool())
        {
            QString t = o["transport"].toString();
            setChannelUi(t);
            addLog(QString("已切换到 %1 通道").arg(t == "zigbee" ? "ZigBee" : "MQTT"));
        }
        else
        {
            addLog(QString("切换失败: %1")
                       .arg(o["message"].toString().isEmpty()
                                ? "通道未就绪"
                                : o["message"].toString()));
        }
    });
}

void MainWindow::setChannelUi(const QString &transport)
{
    m_transport = transport;
    m_channelBtn->setText(transport == "zigbee" ? "通道:ZigBee" : "通道:MQTT");
}

// ------------------------------------------------------------
// 摄像头操作
// ------------------------------------------------------------
void MainWindow::onStartStream()
{
    if (m_streamStarted)
        return;
    addLog("正在启动视频流...");
    httpGet("/api/camera/start_stream", [this](QNetworkReply *r) {
        QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o["ok"].toBool())
        {
            addLog("视频流已启动(画面加载中...)");
            m_streamStarted = true;
            m_startStreamBtn->setEnabled(false);
            m_stopStreamBtn->setEnabled(true);
            startVideoStream(); // 直接发起,readyRead 会等数据
        }
        else
        {
            addLog("视频流启动失败");
        }
    });
}

void MainWindow::onStopStream()
{
    if (!m_streamStarted)
        return;
    addLog("正在停止视频流...");
    // 若正在录像,先停录像(web M6 修复同款:避免 ffmpeg 异常退出留损坏文件)
    if (m_recordStarted)
    {
        httpGet("/api/camera/stop_record", [this](QNetworkReply *) {
            m_recordStarted = false;
            m_recordBtn->setText("视频录制");
            addLog("录像已随推流停止");
            httpGet("/api/camera/stop_stream", [this](QNetworkReply *) {
                finishStopStream();
            });
        });
        return;
    }
    httpGet("/api/camera/stop_stream", [this](QNetworkReply *) {
        finishStopStream();
    });
}

void MainWindow::finishStopStream()
{
    stopVideoStream();
    m_streamStarted = false;
    m_startStreamBtn->setEnabled(true);
    m_stopStreamBtn->setEnabled(false);
    addLog("视频流已停止");
}

void MainWindow::onSnapshot()
{
    addLog("正在拍照...");
    httpGet("/api/camera/snapshot", [this](QNetworkReply *r) {
        QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o["ok"].toBool())
            addLog("拍照成功!照片已保存至 snapshots/");
        else
            addLog("拍照失败");
    });
}

void MainWindow::onToggleRecord()
{
    if (!m_streamStarted)
    {
        addLog("请先启动视频流再进行录像");
        return;
    }
    if (!m_recordStarted)
    {
        addLog("正在启动录像...");
        httpGet("/api/camera/start_record", [this](QNetworkReply *r) {
            QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            if (o["ok"].toBool())
            {
                m_recordStarted = true;
                m_recordBtn->setText("停止录制");
                addLog("录像已开始");
            }
            else
                addLog("录像启动失败");
        });
    }
    else
    {
        addLog("正在停止录像...");
        httpGet("/api/camera/stop_record", [this](QNetworkReply *) {
            m_recordStarted = false;
            m_recordBtn->setText("视频录制");
            addLog("录像已保存");
        });
    }
}

// ------------------------------------------------------------
// MJPEG 流:GET 8080/?action=stream,解析 multipart 帧显示
// ------------------------------------------------------------
void MainWindow::startVideoStream()
{
    if (m_videoReply)
        return;
    QNetworkRequest req(QUrl(QString("http://%1:%2/?action=stream").arg(m_ip).arg(m_videoPort)));
    m_videoReply = m_http.get(req);
    m_videoBuffer.clear();
    connect(m_videoReply, &QNetworkReply::readyRead, this, &MainWindow::onVideoReadyRead);
    connect(m_videoReply, &QNetworkReply::finished, this, &MainWindow::onVideoFinished);
}

void MainWindow::stopVideoStream()
{
    if (m_videoReply)
    {
        m_videoReply->abort();
        m_videoReply->deleteLater();
        m_videoReply = nullptr;
    }
    m_videoBuffer.clear();
    m_lastFrame = QPixmap();
    m_videoLabel->setPixmap(QPixmap());
    m_videoLabel->setText("摄像头画面(点\"开始推流\"加载)");
}

void MainWindow::onVideoReadyRead()
{
    if (!m_videoReply)
        return;
    m_videoBuffer.append(m_videoReply->readAll());
    // 解析 JPEG 帧:FFD8 ... FFD9
    int start = m_videoBuffer.indexOf("\xFF\xD8");
    while (start >= 0)
    {
        int end = m_videoBuffer.indexOf("\xFF\xD9", start + 2);
        if (end < 0)
            break; // 帧不完整,等更多数据
        QByteArray jpeg = m_videoBuffer.mid(start, end - start + 2);
        QPixmap pm;
        if (pm.loadFromData(jpeg, "JPG"))
        {
            m_lastFrame = pm;
            m_videoLabel->setPixmap(pm.scaled(m_videoLabel->size(),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
        }
        m_videoBuffer.remove(0, end + 2);
        start = m_videoBuffer.indexOf("\xFF\xD8");
    }
    // 防缓冲无界增长(垃圾数据/断流)
    if (m_videoBuffer.size() > 2 * 1024 * 1024)
        m_videoBuffer.clear();
}

void MainWindow::onVideoFinished()
{
    if (m_videoReply)
    {
        m_videoReply->deleteLater();
        m_videoReply = nullptr;
    }
    m_videoBuffer.clear();
    if (m_streamStarted)
        addLog("视频流连接断开");
}
