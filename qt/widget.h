#ifndef WIDGET_H
#define WIDGET_H

#include <QByteArray>
#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QLabel;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class GatewayClient;
class WsClient;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(const QString &host, QWidget *parent = nullptr);
    ~Widget();

private slots:
    void startViewing();
    void stopViewing();
    void onStreamData();
    void toggleRecord();
    void setMotorDirection(bool reverse);
    void sendControl();
    void updateStatus(const QJsonObject &status);
    void appendLog(const QString &message);

private:
    void renderNextFrame();

    GatewayClient *client_;
    WsClient *wsClient_;
    QNetworkReply *streamReply_;
    QByteArray streamBuffer_;
    bool viewing_;
    bool recording_;
    int motorDirection_;
    QLabel *connectionLabel_;
    QPushButton *channelButton_;
    QLabel *videoLabel_;
    QPushButton *viewStartButton_;
    QPushButton *viewStopButton_;
    QPushButton *recordButton_;
    QLabel *tempLabel_;
    QLabel *humiLabel_;
    QLabel *lightLabel_;
    QLabel *irLabel_;
    QCheckBox *ledSwitch_;
    QCheckBox *motorSwitch_;
    QCheckBox *buzzerSwitch_;
    QSlider *ledBrightness_;
    QSlider *motorSpeed_;
    QPushButton *forwardButton_;
    QPushButton *reverseButton_;
    QPlainTextEdit *logBox_;
};

#endif
