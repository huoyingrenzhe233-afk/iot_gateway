#include "widget.h"
#include "gatewayclient.h"
#include "wsclient.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
const QString cardStyle = QStringLiteral("QWidget{background:#ffffff;border-radius:16px;}");
const QString buttonStyle = QStringLiteral(
    "QPushButton{border:0;border-radius:8px;padding:10px;color:white;font-weight:600;}"
    "QPushButton:disabled{background:#cbd5e1;}");
}

Widget::Widget(const QString &host, QWidget *parent)
    : QWidget(parent),
      client_(new GatewayClient(host, this)),
      wsClient_(new WsClient(host, this)),
      streamReply_(nullptr),
      viewing_(false),
      recording_(false),
      motorDirection_(0)
{
    setWindowTitle(QStringLiteral("RK3568 智能网关控制系统"));
    resize(1200, 800);
    setMinimumSize(900, 600);
    setStyleSheet(QStringLiteral("QWidget{font-family:'Noto Sans CJK SC','DejaVu Sans';color:#1f2937;}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(20);

    auto *header = new QWidget(this);
    header->setMinimumHeight(60);
    header->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4f46e5,stop:1 #7c3aed);"
        "border-radius:16px;color:white;"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(25, 0, 15, 0);
    auto *title = new QLabel(QStringLiteral("RK3568 智能网关控制系统"), header);
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:800;color:white;"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    channelButton_ = new QPushButton(QStringLiteral("通道:MQTT"), header);
    channelButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,0.15);border:1px solid rgba(255,255,255,0.4);"
        "border-radius:8px;padding:6px 14px;color:white;}"));
    headerLayout->addWidget(channelButton_);
    connectionLabel_ = new QLabel(QStringLiteral("● 设备连接中"), header);
    connectionLabel_->setStyleSheet(QStringLiteral(
        "color:white;background:rgba(255,255,255,0.2);border-radius:20px;padding:4px 12px;"));
    headerLayout->addWidget(connectionLabel_);
    root->addWidget(header);

    auto *mainLayout = new QHBoxLayout();
    mainLayout->setSpacing(20);
    root->addLayout(mainLayout, 1);

    auto *left = new QVBoxLayout();
    left->setSpacing(20);
    auto *videoCard = new QWidget(this);
    videoCard->setStyleSheet(cardStyle);
    auto *videoLayout = new QVBoxLayout(videoCard);
    auto *videoTitle = new QLabel(QStringLiteral("实时画面监控"), videoCard);
    videoTitle->setStyleSheet(QStringLiteral("font-size:17px;font-weight:700;"));
    videoLayout->addWidget(videoTitle);
    videoLabel_ = new QLabel(QStringLiteral("视频未开启"), videoCard);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumHeight(300);
    videoLabel_->setStyleSheet(QStringLiteral(
        "background:#000000;color:#94a3b8;border:4px solid white;border-radius:12px;"));
    videoLayout->addWidget(videoLabel_, 1);
    auto *videoButtons = new QHBoxLayout();
    viewStartButton_ = new QPushButton(QStringLiteral("开始视频"), videoCard);
    viewStopButton_ = new QPushButton(QStringLiteral("停止视频"), videoCard);
    auto *snapshotButton = new QPushButton(QStringLiteral("抓拍照片"), videoCard);
    recordButton_ = new QPushButton(QStringLiteral("视频录制"), videoCard);
    viewStartButton_->setStyleSheet(buttonStyle + QStringLiteral("QPushButton{background:#3b82f6;}"));
    viewStopButton_->setStyleSheet(buttonStyle + QStringLiteral("QPushButton{background:#64748b;}"));
    snapshotButton->setStyleSheet(buttonStyle + QStringLiteral("QPushButton{background:#10b981;}"));
    recordButton_->setStyleSheet(buttonStyle + QStringLiteral("QPushButton{background:#ef4444;}"));
    viewStopButton_->setEnabled(false);
    videoButtons->addWidget(viewStartButton_);
    videoButtons->addWidget(viewStopButton_);
    videoButtons->addWidget(snapshotButton);
    videoButtons->addWidget(recordButton_);
    videoLayout->addLayout(videoButtons);
    left->addWidget(videoCard, 1);

    auto *sensors = new QHBoxLayout();
    tempLabel_ = new QLabel(QStringLiteral("--"), this);
    humiLabel_ = new QLabel(QStringLiteral("--"), this);
    lightLabel_ = new QLabel(QStringLiteral("--"), this);
    irLabel_ = new QLabel(QStringLiteral("--"), this);
    const QList<QPair<QString, QLabel *>> sensorLabels{
        {QStringLiteral("温度\n°C"), tempLabel_},
        {QStringLiteral("湿度\n% RH"), humiLabel_},
        {QStringLiteral("光照强度\nLux"), lightLabel_},
        {QStringLiteral("红外检测\nStatus"), irLabel_}};
    for (const auto &entry : sensorLabels) {
        auto *box = new QWidget(this);
        box->setStyleSheet(cardStyle);
        auto *layout = new QVBoxLayout(box);
        layout->setAlignment(Qt::AlignCenter);
        auto *name = new QLabel(entry.first.section('\n', 0, 0), box);
        name->setAlignment(Qt::AlignCenter);
        auto *unit = new QLabel(entry.first.section('\n', 1, 1), box);
        unit->setAlignment(Qt::AlignCenter);
        unit->setStyleSheet(QStringLiteral("color:#94a3b8;font-size:12px;"));
        entry.second->setAlignment(Qt::AlignCenter);
        entry.second->setStyleSheet(QStringLiteral("color:#5c67f2;font-size:22px;font-weight:800;"));
        layout->addWidget(name);
        layout->addWidget(entry.second);
        layout->addWidget(unit);
        sensors->addWidget(box);
    }
    left->addLayout(sensors);
    mainLayout->addLayout(left, 7);

    auto *right = new QVBoxLayout();
    right->setSpacing(20);
    auto *control = new QWidget(this);
    control->setStyleSheet(cardStyle + QStringLiteral(
        "QCheckBox{spacing:10px;min-height:40px;color:#1f2937;}"
        "QCheckBox::indicator{width:28px;height:28px;border:2px solid #94a3b8;"
        "border-radius:7px;background:#f8fafc;}"
        "QCheckBox::indicator:hover{border-color:#6366f1;background:#eef2ff;}"
        "QCheckBox::indicator:checked{border-color:#4f46e5;background:#4f46e5;}"
        "QCheckBox::indicator:disabled{border-color:#cbd5e1;background:#e2e8f0;}"
        "QSlider{min-height:36px;}"
        "QSlider::groove:horizontal{height:8px;border-radius:4px;background:#e2e8f0;}"
        "QSlider::sub-page:horizontal{border-radius:4px;background:#5c67f2;}"
        "QSlider::add-page:horizontal{border-radius:4px;background:#e2e8f0;}"
        "QSlider::handle:horizontal{width:28px;height:28px;margin:-10px 0;"
        "border:3px solid #ffffff;border-radius:14px;background:#4f46e5;}"
        "QSlider::handle:horizontal:hover{background:#4338ca;}"
        "QSlider::handle:horizontal:pressed{background:#3730a3;}"
        "QSlider::handle:horizontal:disabled{background:#cbd5e1;}"));
    auto *controlLayout = new QVBoxLayout(control);
    auto *controlTitle = new QLabel(QStringLiteral("硬件外设控制"), control);
    controlTitle->setStyleSheet(QStringLiteral("font-size:17px;font-weight:700;"));
    controlLayout->addWidget(controlTitle);
    auto addSwitch = [control, controlLayout](const QString &name, QCheckBox **target) {
        auto *row = new QHBoxLayout();
        auto *label = new QLabel(name, control);
        label->setStyleSheet(QStringLiteral("font-weight:600;"));
        *target = new QCheckBox(control);
        row->addWidget(label);
        row->addStretch();
        row->addWidget(*target);
        controlLayout->addLayout(row);
    };
    addSwitch(QStringLiteral("LED 照明灯"), &ledSwitch_);
    ledBrightness_ = new QSlider(Qt::Horizontal, control);
    ledBrightness_->setRange(0, 100);
    ledBrightness_->setValue(50);
    controlLayout->addWidget(ledBrightness_);
    addSwitch(QStringLiteral("直流电机控制"), &motorSwitch_);
    motorSpeed_ = new QSlider(Qt::Horizontal, control);
    motorSpeed_->setRange(0, 100);
    motorSpeed_->setValue(30);
    controlLayout->addWidget(motorSpeed_);
    auto *directions = new QHBoxLayout();
    forwardButton_ = new QPushButton(QStringLiteral("正向旋转"), control);
    reverseButton_ = new QPushButton(QStringLiteral("反向旋转"), control);
    forwardButton_->setStyleSheet(buttonStyle + QStringLiteral(
        "QPushButton{background:#4f46e5;min-height:42px;}"
        "QPushButton:hover{background:#4338ca;}"
        "QPushButton:pressed{background:#3730a3;}"));
    reverseButton_->setStyleSheet(buttonStyle + QStringLiteral(
        "QPushButton{background:#64748b;min-height:42px;}"
        "QPushButton:hover{background:#475569;}"
        "QPushButton:pressed{background:#334155;}"));
    directions->addWidget(forwardButton_);
    directions->addWidget(reverseButton_);
    controlLayout->addLayout(directions);
    addSwitch(QStringLiteral("紧急蜂鸣报警"), &buzzerSwitch_);
    right->addWidget(control, 1);

    auto *logCard = new QWidget(this);
    logCard->setStyleSheet(cardStyle);
    auto *logLayout = new QVBoxLayout(logCard);
    auto *logTitle = new QLabel(QStringLiteral("系统运行日志"), logCard);
    logTitle->setStyleSheet(QStringLiteral("font-size:17px;font-weight:700;"));
    logLayout->addWidget(logTitle);
    logBox_ = new QPlainTextEdit(logCard);
    logBox_->setReadOnly(true);
    logBox_->setStyleSheet(QStringLiteral(
        "background:#0f172a;color:#38bdf8;border-radius:12px;font-family:monospace;font-size:12px;"));
    logLayout->addWidget(logBox_, 1);
    right->addWidget(logCard, 1);
    mainLayout->addLayout(right, 4);

    connect(viewStartButton_, &QPushButton::clicked, this, &Widget::startViewing);
    connect(viewStopButton_, &QPushButton::clicked, this, &Widget::stopViewing);
    connect(snapshotButton, &QPushButton::clicked, client_, &GatewayClient::snapshot);
    connect(recordButton_, &QPushButton::clicked, this, &Widget::toggleRecord);
    connect(channelButton_, &QPushButton::clicked, this, [this]() {
        client_->switchChannel(channelButton_->text().contains(QStringLiteral("MQTT"))
                                   ? QStringLiteral("zigbee")
                                   : QStringLiteral("mqtt"));
    });
    connect(ledSwitch_, &QCheckBox::toggled, this, &Widget::sendControl);
    connect(motorSwitch_, &QCheckBox::toggled, this, &Widget::sendControl);
    connect(buzzerSwitch_, &QCheckBox::toggled, this, &Widget::sendControl);
    connect(ledBrightness_, &QSlider::valueChanged, this, &Widget::sendControl);
    connect(motorSpeed_, &QSlider::valueChanged, this, &Widget::sendControl);
    connect(forwardButton_, &QPushButton::clicked, this, [this]() { setMotorDirection(false); });
    connect(reverseButton_, &QPushButton::clicked, this, [this]() { setMotorDirection(true); });
    connect(client_, &GatewayClient::statusReceived, this, &Widget::updateStatus);
    connect(client_, &GatewayClient::logMessage, this, &Widget::appendLog);
    connect(client_, &GatewayClient::connectionChanged, this, [this](bool online) {
        connectionLabel_->setText(online ? QStringLiteral("● 设备在线") : QStringLiteral("● 连接断开"));
        connectionLabel_->setStyleSheet(online
            ? QStringLiteral("color:white;background:rgba(255,255,255,0.2);border-radius:20px;padding:4px 12px;")
            : QStringLiteral("color:#ff4d4d;background:rgba(255,255,255,0.2);border-radius:20px;padding:4px 12px;"));
    });
    connect(client_, &GatewayClient::channelReceived, this, [this](const QString &transport) {
        channelButton_->setText(transport == QStringLiteral("zigbee")
                                     ? QStringLiteral("通道:ZigBee")
                                     : QStringLiteral("通道:MQTT"));
    });
    connect(client_, &GatewayClient::cameraStatusReceived, this, [this](bool running, bool recording) {
        recording_ = recording;
        recordButton_->setText(recording ? QStringLiteral("停止录制") : QStringLiteral("视频录制"));
        if (running) appendLog(QStringLiteral("视频流已就绪，点开始视频观看"));
    });
    connect(wsClient_, &WsClient::mqttMessage, this, [this](const QString &topic, const QString &payload) {
        appendLog(QStringLiteral("MQTT %1: %2").arg(topic, payload));
    });
    connect(wsClient_, &WsClient::channelChanged, this, [this](const QString &transport) {
        channelButton_->setText(transport == QStringLiteral("zigbee")
                                     ? QStringLiteral("通道:ZigBee")
                                     : QStringLiteral("通道:MQTT"));
    });
    connect(wsClient_, &WsClient::logMessage, this, &Widget::appendLog);
    connect(client_, &GatewayClient::streamReady, this, [this, host]() {
        QTimer::singleShot(1500, this, [this, host]() {
            auto *manager = new QNetworkAccessManager(this);
            streamReply_ = manager->get(QNetworkRequest(
                QUrl(QStringLiteral("http://%1:8080/?action=stream").arg(host))));
            connect(streamReply_, &QNetworkReply::readyRead, this, &Widget::onStreamData);
        });
    });
    appendLog(QStringLiteral("[系统] 页面加载完成，设备控制就绪"));
    client_->start();
    wsClient_->start();
}

Widget::~Widget()
{
    if (streamReply_) streamReply_->abort();
}

void Widget::startViewing()
{
    appendLog(QStringLiteral("正在开启画面..."));
    viewing_ = true;
    client_->ensureStream();
    viewStartButton_->setEnabled(false);
    viewStopButton_->setEnabled(true);
}

void Widget::stopViewing()
{
    viewing_ = false;
    if (streamReply_) {
        streamReply_->abort();
        streamReply_->deleteLater();
        streamReply_ = nullptr;
    }
    streamBuffer_.clear();
    videoLabel_->clear();
    videoLabel_->setText(QStringLiteral("视频已停止"));
    viewStartButton_->setEnabled(true);
    viewStopButton_->setEnabled(false);
    appendLog(QStringLiteral("已停止观看(不影响其他端的画面)"));
}

void Widget::onStreamData()
{
    if (!viewing_ || !streamReply_) return;
    streamBuffer_.append(streamReply_->readAll());
    renderNextFrame();
}

void Widget::renderNextFrame()
{
    const QByteArray soi = QByteArray::fromHex("ffd8");
    const QByteArray eoi = QByteArray::fromHex("ffd9");
    const int start = streamBuffer_.indexOf(soi);
    const int end = streamBuffer_.indexOf(eoi, start + soi.size());
    if (start < 0 || end < 0) {
        if (streamBuffer_.size() > 2 * 1024 * 1024) streamBuffer_.clear();
        return;
    }
    const QByteArray jpeg = streamBuffer_.mid(start, end - start + eoi.size());
    streamBuffer_.remove(0, end + eoi.size());
    QImage image;
    if (image.loadFromData(jpeg, "JPG")) {
        videoLabel_->setPixmap(QPixmap::fromImage(image).scaled(
            videoLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (streamBuffer_.contains(soi)) renderNextFrame();
}

void Widget::toggleRecord()
{
    if (recording_) client_->stopRecord();
    else client_->startRecord();
    recording_ = !recording_;
    recordButton_->setText(recording_ ? QStringLiteral("停止录制") : QStringLiteral("视频录制"));
}

void Widget::setMotorDirection(bool reverse)
{
    motorDirection_ = reverse ? 1 : 0;
    sendControl();
}

void Widget::sendControl()
{
    client_->sendControl(QJsonObject{
        {QStringLiteral("led_on"), ledSwitch_->isChecked() ? 1 : 0},
        {QStringLiteral("led_br"), ledBrightness_->value()},
        {QStringLiteral("motor_on"), motorSwitch_->isChecked() ? 1 : 0},
        {QStringLiteral("motor_sp"), motorSpeed_->value()},
        {QStringLiteral("motor_dir"), motorDirection_},
        {QStringLiteral("buzzer"), buzzerSwitch_->isChecked() ? 1 : 0}});
}

void Widget::updateStatus(const QJsonObject &status)
{
    const QSignalBlocker ledBlocker(ledSwitch_);
    const QSignalBlocker motorBlocker(motorSwitch_);
    const QSignalBlocker buzzerBlocker(buzzerSwitch_);
    const QSignalBlocker ledBrightnessBlocker(ledBrightness_);
    const QSignalBlocker motorSpeedBlocker(motorSpeed_);
    const auto sensor = [&status](const char *key) {
        const QString value = status.value(QString::fromLatin1(key)).toString();
        return value.isEmpty() ? QStringLiteral("--") : value;
    };
    tempLabel_->setText(sensor("temp"));
    humiLabel_->setText(sensor("humi"));
    lightLabel_->setText(sensor("light"));
    const bool detected = status.value(QStringLiteral("ir")).toString().toDouble() > 2000;
    irLabel_->setText(detected ? QStringLiteral("有人") : QStringLiteral("安全"));
    irLabel_->setStyleSheet(QStringLiteral("color:%1;font-size:16px;font-weight:800;")
                                .arg(detected ? QStringLiteral("#ef4444") : QStringLiteral("#10b981")));
    ledSwitch_->setChecked(status.value(QStringLiteral("led_on")).toInt() == 1);
    motorSwitch_->setChecked(status.value(QStringLiteral("motor_on")).toInt() == 1);
    buzzerSwitch_->setChecked(status.value(QStringLiteral("buzzer")).toInt() == 1);
    if (!ledBrightness_->hasFocus()) ledBrightness_->setValue(status.value(QStringLiteral("led_br")).toInt(50));
    if (!motorSpeed_->hasFocus()) motorSpeed_->setValue(status.value(QStringLiteral("motor_sp")).toInt(30));
    motorDirection_ = status.value(QStringLiteral("motor_dir")).toInt();
}

void Widget::appendLog(const QString &message)
{
    logBox_->appendPlainText(QStringLiteral("[%1] %2")
                                 .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
    while (logBox_->blockCount() > 300) {
        QTextCursor cursor(logBox_->document());
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
    logBox_->verticalScrollBar()->setValue(logBox_->verticalScrollBar()->maximum());
}
