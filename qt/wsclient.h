#ifndef WSCLIENT_H
#define WSCLIENT_H

#include <QObject>
#include <QByteArray>

class QTcpSocket;
class QTimer;

class WsClient : public QObject
{
    Q_OBJECT

public:
    explicit WsClient(const QString &host, QObject *parent = nullptr);
    void start();

signals:
    void mqttMessage(const QString &topic, const QString &payload);
    void channelChanged(const QString &transport);
    void logMessage(const QString &message);

private slots:
    void connectSocket();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();

private:
    void parseHandshake();
    void parseFrames();
    void sendFrame(const QByteArray &payload, quint8 opcode);
    void scheduleReconnect();

    QTcpSocket *socket_;
    QTimer *reconnectTimer_;
    QString host_;
    QByteArray buffer_;
    QByteArray handshakeKey_;
    bool handshakeComplete_;
    bool reconnectScheduled_;
};

#endif
