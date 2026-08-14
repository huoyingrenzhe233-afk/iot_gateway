#include "wsclient.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

WsClient::WsClient(const QString &host, QObject *parent)
    : QObject(parent),
      socket_(new QTcpSocket(this)),
      reconnectTimer_(new QTimer(this)),
      host_(host),
      handshakeComplete_(false),
      reconnectScheduled_(false)
{
    reconnectTimer_->setSingleShot(true);
    reconnectTimer_->setInterval(3000);
    connect(reconnectTimer_, &QTimer::timeout, this, &WsClient::connectSocket);
    connect(socket_, &QTcpSocket::connected, this, &WsClient::onConnected);
    connect(socket_, &QTcpSocket::readyRead, this, &WsClient::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &WsClient::onDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, &WsClient::onSocketError);
}

void WsClient::start()
{
    connectSocket();
}

void WsClient::connectSocket()
{
    reconnectScheduled_ = false;
    if (socket_->state() != QAbstractSocket::UnconnectedState) return;
    handshakeComplete_ = false;
    buffer_.clear();
    socket_->connectToHost(host_, 8081);
}

void WsClient::onConnected()
{
    handshakeKey_ = QByteArray::number(QRandomGenerator::global()->generate64()).toBase64();
    const QByteArray request = "GET /ws HTTP/1.1\r\n"
                               "Host: " + host_.toUtf8() + ":8081\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Key: " + handshakeKey_ + "\r\n"
                               "Sec-WebSocket-Version: 13\r\n\r\n";
    socket_->write(request);
}

void WsClient::onReadyRead()
{
    buffer_.append(socket_->readAll());
    if (!handshakeComplete_) parseHandshake();
    if (handshakeComplete_) parseFrames();
}

void WsClient::parseHandshake()
{
    const int end = buffer_.indexOf("\r\n\r\n");
    if (end < 0) return;
    const QByteArray response = buffer_.left(end + 4);
    buffer_.remove(0, end + 4);
    const QByteArray expected = QCryptographicHash::hash(
        handshakeKey_ + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
    if (!response.startsWith("HTTP/1.1 101") || !response.contains(expected)) {
        emit logMessage(QStringLiteral("WebSocket 握手失败"));
        socket_->disconnectFromHost();
        return;
    }
    handshakeComplete_ = true;
    emit logMessage(QStringLiteral("🔌 WebSocket 已连接(实时推送)"));
}

void WsClient::parseFrames()
{
    while (buffer_.size() >= 2) {
        const quint8 first = static_cast<quint8>(buffer_.at(0));
        const quint8 second = static_cast<quint8>(buffer_.at(1));
        const quint8 opcode = first & 0x0f;
        quint64 length = second & 0x7f;
        int headerSize = 2;
        if (length == 126) {
            if (buffer_.size() < 4) return;
            length = (static_cast<quint8>(buffer_.at(2)) << 8) |
                     static_cast<quint8>(buffer_.at(3));
            headerSize = 4;
        } else if (length == 127) {
            if (buffer_.size() < 10) return;
            length = 0;
            for (int i = 0; i < 8; ++i) length = (length << 8) | static_cast<quint8>(buffer_.at(2 + i));
            headerSize = 10;
        }
        if (length > static_cast<quint64>(buffer_.size() - headerSize)) return;
        const QByteArray payload = buffer_.mid(headerSize, static_cast<int>(length));
        buffer_.remove(0, headerSize + static_cast<int>(length));
        if (opcode == 0x1) {
            const QJsonDocument document = QJsonDocument::fromJson(payload);
            if (!document.isObject()) continue;
            const QJsonObject message = document.object();
            const QString type = message.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("mqtt_msg")) {
                emit mqttMessage(message.value(QStringLiteral("topic")).toString(),
                                 message.value(QStringLiteral("payload")).toString());
            } else if (type == QStringLiteral("channel")) {
                emit channelChanged(message.value(QStringLiteral("transport")).toString());
            }
        } else if (opcode == 0x8) {
            socket_->disconnectFromHost();
            return;
        } else if (opcode == 0x9) {
            sendFrame(payload, 0xA);
        }
    }
}

void WsClient::sendFrame(const QByteArray &payload, quint8 opcode)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    const quint64 length = static_cast<quint64>(payload.size());
    if (length < 126) {
        frame.append(static_cast<char>(0x80 | length));
    } else if (length <= 0xffff) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((length >> 8) & 0xff));
        frame.append(static_cast<char>(length & 0xff));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) frame.append(static_cast<char>((length >> shift) & 0xff));
    }
    QByteArray mask(4, Qt::Uninitialized);
    const quint32 value = QRandomGenerator::global()->generate();
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<char>((value >> (i * 8)) & 0xff);
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) masked[i] = static_cast<char>(masked.at(i) ^ mask.at(i % 4));
    socket_->write(frame + masked);
}

void WsClient::onDisconnected()
{
    handshakeComplete_ = false;
    scheduleReconnect();
}

void WsClient::onSocketError()
{
    if (socket_->state() == QAbstractSocket::UnconnectedState) scheduleReconnect();
}

void WsClient::scheduleReconnect()
{
    if (reconnectScheduled_) return;
    reconnectScheduled_ = true;
    emit logMessage(QStringLiteral("WebSocket 已断开，3 秒后重连"));
    reconnectTimer_->start();
}
