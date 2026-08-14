#include "gatewayclient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

GatewayClient::GatewayClient(const QString &host, QObject *parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      baseUrl_(QStringLiteral("http://%1:8081").arg(host)),
      pollTimer_(new QTimer(this)),
      polling_(false)
{
    pollTimer_->setInterval(1000);
    connect(pollTimer_, &QTimer::timeout, this, &GatewayClient::pollStatus);
}

void GatewayClient::start()
{
    pollTimer_->start();
    fetchChannel();
    fetchCameraStatus();
    pollStatus();
}

void GatewayClient::get(const QString &path, const std::function<void(const QJsonObject &)> &onSuccess,
                        const std::function<void()> &onFailure)
{
    QNetworkReply *reply = network_->get(QNetworkRequest(baseUrl_.resolved(QUrl(path))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onFailure]() {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectionChanged(false);
            emit logMessage(QStringLiteral("接口请求失败: %1").arg(reply->errorString()));
            if (onFailure) onFailure();
        } else {
            const QJsonDocument document = QJsonDocument::fromJson(data);
            if (document.isObject()) {
                emit connectionChanged(true);
                onSuccess(document.object());
            } else {
                emit logMessage(QStringLiteral("接口返回不是 JSON 对象"));
            }
        }
        reply->deleteLater();
    });
}

void GatewayClient::post(const QString &path, const QJsonObject &body,
                         const std::function<void(const QJsonObject &)> &onSuccess)
{
    QNetworkRequest request(baseUrl_.resolved(QUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess]() {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit logMessage(QStringLiteral("接口请求失败: %1").arg(reply->errorString()));
        } else {
            const QJsonDocument document = QJsonDocument::fromJson(data);
            if (document.isObject()) onSuccess(document.object());
        }
        reply->deleteLater();
    });
}

void GatewayClient::pollStatus()
{
    if (polling_) return;
    polling_ = true;
    get(QStringLiteral("/api/status"), [this](const QJsonObject &status) {
        polling_ = false;
        emit statusReceived(status);
    }, [this]() { polling_ = false; });
}

void GatewayClient::fetchChannel()
{
    get(QStringLiteral("/api/channel"), [this](const QJsonObject &data) {
        emit channelReceived(data.value(QStringLiteral("transport")).toString());
    });
}

void GatewayClient::fetchCameraStatus()
{
    get(QStringLiteral("/api/camera/status"), [this](const QJsonObject &data) {
        emit cameraStatusReceived(data.value(QStringLiteral("running")).toBool(),
                                  data.value(QStringLiteral("recording")).toBool());
    });
}

void GatewayClient::sendControl(const QJsonObject &payload)
{
    const QJsonObject body{{QStringLiteral("type"), QStringLiteral("control")},
                           {QStringLiteral("payload"), payload}};
    post(QStringLiteral("/api/control"), body,
         [this](const QJsonObject &) { emit logMessage(QStringLiteral("指令已发送")); });
}

void GatewayClient::ensureStream()
{
    get(QStringLiteral("/api/camera/start_stream"), [this](const QJsonObject &) { emit streamReady(); });
}

void GatewayClient::snapshot()
{
    post(QStringLiteral("/api/camera/snapshot"), QJsonObject(),
         [this](const QJsonObject &data) {
             emit logMessage(QStringLiteral("拍照成功: %1").arg(data.value(QStringLiteral("filename")).toString()));
         });
}

void GatewayClient::startRecord()
{
    post(QStringLiteral("/api/camera/start_record"), QJsonObject(),
         [this](const QJsonObject &) { emit logMessage(QStringLiteral("录像已开始")); });
}

void GatewayClient::stopRecord()
{
    post(QStringLiteral("/api/camera/stop_record"), QJsonObject(),
         [this](const QJsonObject &) { emit logMessage(QStringLiteral("录像已保存")); });
}

void GatewayClient::switchChannel(const QString &transport)
{
    post(QStringLiteral("/api/channel/switch"),
         QJsonObject{{QStringLiteral("transport"), transport}},
         [this, transport](const QJsonObject &) {
             emit channelReceived(transport);
             emit logMessage(QStringLiteral("已切换到 %1 通道").arg(
                 transport == QStringLiteral("zigbee") ? QStringLiteral("ZigBee") : QStringLiteral("MQTT")));
         });
}
