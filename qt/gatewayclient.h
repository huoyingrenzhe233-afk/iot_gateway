#ifndef GATEWAYCLIENT_H
#define GATEWAYCLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QTimer;

class GatewayClient : public QObject
{
    Q_OBJECT

public:
    explicit GatewayClient(const QString &host, QObject *parent = nullptr);
    void start();
    void sendControl(const QJsonObject &payload);
    void ensureStream();
    void snapshot();
    void startRecord();
    void stopRecord();
    void switchChannel(const QString &transport);

signals:
    void statusReceived(const QJsonObject &status);
    void cameraStatusReceived(bool running, bool recording);
    void channelReceived(const QString &transport);
    void connectionChanged(bool online);
    void logMessage(const QString &message);
    void streamReady();

private slots:
    void pollStatus();
    void fetchChannel();
    void fetchCameraStatus();

private:
    void get(const QString &path, const std::function<void(const QJsonObject &)> &onSuccess,
             const std::function<void()> &onFailure = std::function<void()>());
    void post(const QString &path, const QJsonObject &body,
              const std::function<void(const QJsonObject &)> &onSuccess);

    QNetworkAccessManager *network_;
    QUrl baseUrl_;
    QTimer *pollTimer_;
    bool polling_;
};

#endif
