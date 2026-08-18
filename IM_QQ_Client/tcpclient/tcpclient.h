#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>

class TcpClient : public QObject
{
    Q_OBJECT
public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();
    
    void connectToServer(const QString &host, quint16 port);

    void disconnectFromServer();

    // 发送一行 JSON（不含 \n，本类内部自动追加 \n 并 flush）
    void sendFrame(const QString &line);

    // 当前是否处于已连接状态
    bool isConnected() const;
signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void frameReceived(const QString &line);    // 切好的一帧（不含 \n）

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);

private:
    QTcpSocket *m_socket = nullptr;
    QByteArray m_recvBuffer;  // 接收缓冲区，处理粘包/半包
    QString m_host;
    quint16 m_port;
};
#endif // TCPCLIENT_H
