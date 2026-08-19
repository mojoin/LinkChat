#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QFile>
#include <QtGlobal>

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

    // 开始以"4字节网络序长度 + 数据块"帧格式分块发送一个文件，
    // 文件发完会追加 4 字节 0(FIN)。进度/完成/错误通过信号回调。
    // 调用前请确认服务端已进入 BINARY_UPLOAD 模式(收到 upload_ready)。
    bool sendBinaryFile(const QString &filePath);

    // 中止正在进行的文件发送
    void cancelBinarySend();

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void frameReceived(const QString &line);    // 切好的一帧（不含 \n）

    // 二进制文件发送进度（已发字节 / 总字节）
    void fileProgress(qint64 sent, qint64 total);
    // 文件全部发送完成（已追加 FIN）
    void fileSendFinished();
    // 文件发送失败（打开文件失败 / 断开等）
    void fileSendError(const QString &message);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onBytesWritten(qint64 bytes);

private:
    QTcpSocket *m_socket = nullptr;
    QByteArray m_recvBuffer;  // 接收缓冲区，处理粘包/半包
    QString m_host;
    quint16 m_port;

    // ---- 二进制文件发送状态 ----
    QFile     m_file;          // 正在发送的文件
    qint64    m_fileTotal = 0; // 文件总字节
    qint64    m_fileSent  = 0; // 已发送的数据字节（不含帧头/FIN）
    bool      m_fileSending = false;    // 防止一个tcp传多个文件
    bool      m_finSent      = false;  // FIN 是否已成功入队
    const qint64 CHUNK = 32 * 1024;    // 与服务端 CHUNK 一致
    const int  MAX_WRITES_PER_CALL = 64; // 单次事件最多写多少块，防阻塞事件循环

    // 把剩余数据塞进 socket 缓冲，发多少算多少；缓冲满返回 false
    bool tryFlushSend();
};
#endif // TCPCLIENT_H
