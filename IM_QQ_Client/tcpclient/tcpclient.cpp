#include "tcpclient.h"
#include <QDebug>

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
    emit disconnected();
}

void TcpClient::onErrorOccurred(QAbstractSocket::SocketError /*socketError*/)
{
    QString err = m_socket ? m_socket->errorString() : QStringLiteral("unknown");
    qDebug() << "[TcpClient] 错误:" << err;
    emit errorOccurred(err);
}

void TcpClient::onReadyRead()
{
    if (!m_socket) return;

    // 把刚到的字节追加到缓冲区
    m_recvBuffer.append(m_socket->readAll());

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
