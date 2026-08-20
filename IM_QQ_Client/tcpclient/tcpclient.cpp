#include "tcpclient.h"
#include <QDebug>
#include <QDataStream>
#include <QtEndian>
#include <cstring>

TcpClient::TcpClient(QObject *parent) 
    : QObject(parent) 
    , m_host()
    , m_port(0)
{}

TcpClient::~TcpClient()
{
    // m_socket 父对象为 this，会被 Qt 自动 delete，无需手动释放
}

void TcpClient::connectToServer(const QString &host, quint16 port)
{
    // 已经在连接或已连接，则不重复连接
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        qDebug() << "[TcpClient] 已处于连接中，无需重复连接";
        return;
    }

    m_host = host;
    m_port = port;

    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &TcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &TcpClient::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &TcpClient::onErrorOccurred);
    connect(m_socket, &QTcpSocket::bytesWritten,
            this, &TcpClient::onBytesWritten);

    qDebug() << "[TcpClient] 正在连接" << host << ":" << port;
    m_socket->connectToHost(host, port);
}

void TcpClient::disconnectFromServer()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
    }
}

void TcpClient::sendFrame(const QString &line)
{
    if (!m_socket || !m_socket->isOpen()) {
        qDebug() << "[TcpClient] 未连接，丢弃帧:" << line;
        return;
    }
    // 调用方传的是一行 JSON，不含 \n
    // 由本类统一追加 \n 并 flush
    QByteArray data = line.toUtf8() + "\n";
    m_socket->write(data);
    m_socket->flush();
}

bool TcpClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}



bool TcpClient::sendBinaryFile(const QString &filePath)
{
    if (m_fileSending)
    {
        qDebug() << "[TcpClient] 上一个文件仍在发送中";
        return false;
    }
    if (!m_socket || !m_socket->isOpen())
    {
        qDebug() << "[TcpClient] 未连接，无法发送文件";
        emit fileSendError(QStringLiteral("未连接服务器"));
        return false;
    }

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly))
    {
        qDebug() << "[TcpClient] 打开文件失败:" << filePath;
        emit fileSendError(QStringLiteral("无法打开文件"));
        return false;
    }

    m_fileTotal   = m_file.size();
    m_fileSent    = 0;
    m_fileSending = true;
    m_finSent     = false;

    // 先发第一块；后续由 bytesWritten 推进
    tryFlushSend();
    return true;
}

bool TcpClient::startBinaryRecv(const QString &transferId, const QString &localPath, qint64 fileSize)
{
    if (m_downloading)
    {
        qDebug() << "[TcpClient] 已在接收文件中,无法开始新接收";
        return false;
    }
    if (!m_socket || !m_socket->isOpen())
    {
        qDebug() << "[TcpClient] 未连接,无法接收文件";
        emit fileRecvError(QStringLiteral("未连接服务器"));
        return false;
    }

    m_recvFile.setFileName(localPath);
    if (!m_recvFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qDebug() << "[TcpClient] 打开本地文件失败:" << localPath;
        emit fileRecvError(QStringLiteral("无法打开本地文件"));
        return false;
    }

    m_downloading    = true;
    m_recvTransferId = transferId;
    m_recvFileSize   = fileSize;
    m_recvFileGot    = 0;
    m_recvExpected   = 0;   // 0 = 等下一个 4 字节长度头

    qDebug() << "[TcpClient] 开始接收文件:" << transferId
             << "→" << localPath << "size=" << fileSize;
    return true;
}

void TcpClient::cancelBinarySend()
{
    m_fileSending = false;
    m_finSent     = false;
    if (m_file.isOpen())
        m_file.close();
    m_fileTotal = 0;
    m_fileSent  = 0;
}

void TcpClient::cancelBinaryRecv()
{
    if (!m_downloading) return;

    qDebug() << "[TcpClient] 取消接收文件:" << m_recvTransferId;
    m_downloading    = false;
    m_recvExpected   = 0;
    m_recvFileSize   = 0;
    m_recvFileGot    = 0;
    QString tid      = m_recvTransferId;
    QString path     = m_recvFile.fileName();
    m_recvTransferId.clear();

    if (m_recvFile.isOpen())
        m_recvFile.close();

    // 半成品文件保留在磁盘(用户可手动删除)
    emit fileRecvError(QStringLiteral("用户取消:") + tid);
}


// 尝试把剩余数据塞进 socket 缓冲，发多少算多少。
// 返回 true 表示还有空间继续 / 已全部完成；返回 false 表示缓冲满，需等下次 bytesWritten。
bool TcpClient::tryFlushSend()
{
    if (!m_fileSending || !m_file.isOpen())
        return false;

    // 数据未发完：读一块塞进缓冲，尊重 write() 返回值
    if (m_fileSent < m_fileTotal)
    {
        QByteArray block = m_file.read(CHUNK);
        if (block.isEmpty())
            return false; // 读不到更多数据

        // 帧 = 4 字节网络序长度 + 数据
        QByteArray frame;
        QDataStream ds(&frame, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << (quint32)block.size();
        frame.append(block);

        qint64 n = m_socket->write(frame);
        if (n <= 0)
        {
            // 缓冲满/入队失败：回退已读的位置，块留在下次再发
            m_file.seek(m_file.pos() - block.size());
            return false;
        }
        // 只累加"真正入队的数据字节"（write 可能部分入队，但此处以数据块计）
        m_fileSent += block.size();

        emit fileProgress(m_fileSent, m_fileTotal);
        return true;
    }

    // 数据已全部入队：发 FIN（4 字节 0），确认入队后才算完成
    if (!m_finSent)
    {
        QByteArray fin;
        QDataStream ds(&fin, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << (quint32)0;

        if (m_socket->write(fin) == 4)
        {
            m_finSent     = true;
            m_fileSending = false;
            m_file.close();
            emit fileSendFinished();
        }
        // 写不进去，等下次 bytesWritten 再试
    }
    return false;
}

// 下载模式的核心循环:从 m_recvBuffer 消费 4字节长度 + 数据块
// 长度头为 0 表示 FIN,接收结束
void TcpClient::processBinaryRecv()
{
    while (m_downloading && m_recvBuffer.size() >= 4)
    {
        // 1. 等下一个长度头
        if (m_recvExpected == 0)
        {
            quint32 len_be = 0;
            memcpy(&len_be, m_recvBuffer.constData(), 4);
            m_recvExpected = qFromBigEndian(len_be);

            // 移除 4 字节长度头
            m_recvBuffer.remove(0, 4);

            if (m_recvExpected == 0)
            {
                // 收到 FIN,接收完成
                QString tid = m_recvTransferId;
                QString path = m_recvFile.fileName();
                if (m_recvFile.isOpen())
                    m_recvFile.close();

                m_downloading = false;
                m_recvTransferId.clear();
                m_recvFileSize = 0;
                m_recvFileGot = 0;

                qDebug() << "[TcpClient] 文件接收完成:" << tid << "→" << path;
                emit fileRecvFinished(tid, path);
                return;
            }
        }

        // 2. 数据块还没攒够,等下次 readyRead
        if (m_recvBuffer.size() < m_recvExpected)
        {
            return;
        }

        // 3. 取出这一帧数据,写盘
        QByteArray block = m_recvBuffer.left(m_recvExpected);
        qint64 written = m_recvFile.write(block);
        if (written != m_recvExpected)
        {
            // 写盘失败
            QString path = m_recvFile.fileName();
            if (m_recvFile.isOpen())
                m_recvFile.close();

            m_downloading = false;
            m_recvExpected = 0;
            m_recvTransferId.clear();

            qDebug() << "[TcpClient] 写文件失败:" << path;
            emit fileRecvError(QStringLiteral("写文件失败:") + path);
            return;
        }

        // 4. 已写入,推进缓冲和计数
        m_recvBuffer.remove(0, m_recvExpected);
        m_recvFileGot += m_recvExpected;
        m_recvExpected = 0; // 回到等长度头状态

        // 5. 进度回调
        emit fileRecvProgress(m_recvFileGot, m_recvFileSize);
    }
}

void TcpClient::onRecvBytesWritten(qint64 bytes)
{
}

void TcpClient::onBytesWritten(qint64 /*bytes*/)
{
    if (!m_fileSending || !m_file.isOpen())
        return;

    // 缓冲腾出空间就继续推进；单次事件限次，防阻塞事件循环
    for (int i = 0; i < MAX_WRITES_PER_CALL; ++i)
    {
        if (!tryFlushSend())
            break; // 缓冲满或已完成，停下
    }
}

void TcpClient::onConnected()   // 槽函数：连接成功
{
    qDebug() << "[TcpClient] 已连接到服务器" << m_host << ":" << m_port;
    emit connected();
}

void TcpClient::onDisconnected()
{
    qDebug() << "[TcpClient] 与服务器断开连接";
    // 清空半包残留数据，避免重连后污染下一段流
    m_recvBuffer.clear();
    if (m_fileSending)
    {
        m_fileSending = false;
        if (m_file.isOpen())
            m_file.close();
        emit fileSendError(QStringLiteral("连接已断开"));
    }

    // 如果正在下载文件
    if (m_downloading)
    {
        m_downloading    = false;
        m_recvExpected   = 0;
        QString tid      = m_recvTransferId;
        m_recvTransferId.clear();
        if (m_recvFile.isOpen())
            m_recvFile.close();
        emit fileRecvError(QStringLiteral("连接已断开:") + tid);
    }

    emit disconnected();
}

void TcpClient::onErrorOccurred(QAbstractSocket::SocketError /*socketError*/)
{
    QString err = m_socket ? m_socket->errorString() : QStringLiteral("unknown");
    qDebug() << "[TcpClient] 错误:" << err;

    if (m_fileSending)
    {
        m_fileSending = false;
        if (m_file.isOpen())
            m_file.close();
        emit fileSendError(QStringLiteral("发送出错:") + err);
    }

    if (m_downloading)
    {
        m_downloading    = false;
        m_recvExpected   = 0;
        QString tid      = m_recvTransferId;
        m_recvTransferId.clear();
        if (m_recvFile.isOpen())
            m_recvFile.close();
        emit fileRecvError(QStringLiteral("接收出错:") + err);
    }

    emit errorOccurred(err);
}

void TcpClient::onReadyRead()
{
    if (!m_socket) return;

    // 把刚到的字节追加到缓冲区
    m_recvBuffer.append(m_socket->readAll());

    // 下载模式 → 按"4字节长度 + 数据块"协议消费,不再按 \n 切
    if (m_downloading)
    {
        processBinaryRecv();
        return;
    }

    // 循环切出每一行（以 \n 为帧边界）
    while (true) {
        int pos = m_recvBuffer.indexOf('\n');
        if (pos < 0) {
            // 还没攒够一整行，等下次 readyRead
            break;
        }

        // 取出一行（不含 \n）
        QByteArray lineBytes = m_recvBuffer.left(pos);
        // 把已处理的部分从缓冲区移除
        m_recvBuffer.remove(0, pos + 1);

        QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty()) {
            // 空行忽略
            continue;
        }
        emit frameReceived(line);
    }
}
