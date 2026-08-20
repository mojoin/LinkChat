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

    // 开始以"4字节网络序长度 + 数据块"帧格式接收服务器发来的文件。
    // 调用前请确认已收到 download_reply ok=true,并在收到后立即调用。
    // 调用后 TcpClient 进入"下载模式",直到收到 4 字节 0(FIN)或出错。
    // 进度/完成/错误通过 fileRecvProgress / fileRecvFinished / fileRecvError 信号回调。
    bool startBinaryRecv(const QString &transferId,
                     const QString &localPath,
                     qint64 fileSize);

    // 中止正在进行的文件接收
    void cancelBinaryRecv();

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

    // 二进制文件接收进度（已接收字节 / 总字节）
    void fileRecvProgress(qint64 received, qint64 total);
    // 文件全部接收完成（已收到 FIN）
    void fileRecvFinished(const QString &transferId, const QString &localPath);
    // 文件接收失败（断连 / 写文件失败等）
    void fileRecvError(const QString &message);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onBytesWritten(qint64 bytes);
    void processBinaryRecv(); 
    void onRecvBytesWritten(qint64 bytes);

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

    // ---- 二进制文件接收状态 ----
    bool m_downloading = false;       // 是否处于"读二进制流"模式
    QFile m_recvFile;                 // 正在接收的文件(写到本地)
    qint64 m_recvExpected = 0;        // 当前帧预期字节数;0=在等长度头
    qint64 m_recvFileSize = 0;        // 整个文件的总字节数(进度用)
    qint64 m_recvFileGot = 0;         // 已写入本地的字节数
    QString m_recvTransferId;         // 当前正在接收的 transfer_id
    const int MAX_RECV_PER_CALL = 64; // 单次事件最多处理多少帧
};
#endif // TCPCLIENT_H
