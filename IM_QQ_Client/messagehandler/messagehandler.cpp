#include "messagehandler.h"
#include "tcpclient/tcpclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>

MessageHandler::MessageHandler(TcpClient *client, QObject *parent)
    : QObject(parent), m_tcp(client)
{
    // 订阅 TcpClient 的帧信号
    if (m_tcp)
    {
        connect(m_tcp, &TcpClient::frameReceived,
                this, &MessageHandler::onFrame);
    }
}

// ================ 发送 ================
void MessageHandler::sendClientHello()
{
    if (!m_tcp)
        return;
    m_tcp->sendFrame("CLIENT_HELLO 1");
}

void MessageHandler::sendEcho(const QString &msg)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "echo";
    req["msg"] = msg;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendLogin(qint64 uid, const QString &password)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "login";
    req["uid"] = uid;
    req["password"] = password;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
    m_currentUid = uid;
}

void MessageHandler::sendRegister(qint64 uid, const QString &password, const QString &nickname)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "register";
    req["uid"] = uid;
    req["password"] = password;
    req["nickname"] = nickname;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendGetFriends(qint64 uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "get_friends";
    req["uid"] = uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendSearchUser(qint64 uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "search_user";
    req["uid"] = uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendAddFriendRequest(qint64 to_uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "add_friend_request";
    req["to_uid"] = to_uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendListFriendRequests()
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "list_friend_requests";
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendFriendRequestReply(const QString &request_id, bool accept)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "friend_request_reply";
    req["request_id"] = request_id;
    req["accept"] = accept;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendChat(qint64 to_uid, const QString &msg)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "chat";
    req["to"] = to_uid;
    req["msg"] = msg;
    req["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendGetHistory(qint64 peer_uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "get_history";
    req["uid"] = m_currentUid;
    req["peer_uid"] = peer_uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendListFiles(qint64 peer_uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "list_files";
    req["peer_uid"] = peer_uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendDownloadFile(const QString &transfer_id, qint64 peer_uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "download_file";
    req["transfer_id"] = transfer_id;
    req["peer_uid"] = peer_uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void MessageHandler::sendDeleteFile(const QString &transfer_id, qint64 peer_uid)
{
    if (!m_tcp)
        return;
    QJsonObject req;
    req["type"] = "delete_file";
    req["transfer_id"] = transfer_id;
    req["peer_uid"] = peer_uid;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

// ================ 接收 ================
void MessageHandler::onFrame(const QString &line)
{
    if (line.isEmpty())
        return;

    // 分流：JSON 帧走 handleJson，纯文本帧走 handlePlain
    if (line.startsWith('{'))
    {
        handleJson(line);
    }
    else
    {
        handlePlain(line);
    }
}

void MessageHandler::handlePlain(const QString &line)
{
    qDebug() << "[MessageHandler] 文本帧:" << line;

    if (line == "SERVER_HELLO 1")
    {
        // 服务器打招呼，我们回应
        sendClientHello();
        return;
    }

    if (line == "SERVER_WELCOME 1")
    {
        // 握手完成
        if (!m_handshaked)
        {
            m_handshaked = true;
            emit handshakeDone();
        }
        return;
    }

    if (line == "ERR_NEED_LOGIN")
    {
        // 服务器拒绝：需要先登录
        emit errorMessage(QStringLiteral("服务器要求先登录"));
        return;
    }

    // 其它未知文本帧
    qDebug() << "[MessageHandler] 未识别的文本帧:" << line;
}

void MessageHandler::handleJson(const QString &line)
{
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject())
    {
        qDebug() << "[MessageHandler]收到非法 JSON:" << line;
        emit errorMessage(QStringLiteral("非法 JSON 帧"));
        return;
    }
    QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type.isEmpty())
    {
        qDebug() << "[MessageHandler] JSON 缺 type:" << line;
        emit errorMessage(QStringLiteral("JSON 帧缺少 type 字段"));
        return;
    }

    qDebug() << "[MessageHandler] JSON 帧 type=" << type;

    if (type == "friends_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        if (!ok)
        {
            emit errorMessage(obj.value("msg").toString());
            return;
        }
        QList<FriendInfo> list;
        QJsonArray arr = obj.value("friends").toArray();
        for (const auto &v : arr)
        {
            QJsonObject f = v.toObject();
            FriendInfo info;
            info.uid = f.value("uid").toVariant().toLongLong();
            info.nickname = f.value("nickname").toString();
            info.online = f.value("online").toBool(false);
            list.append(info);
        }
        emit friendsReceived(list);
        return;
    }

    if (type == "register_reply")
    {
        bool ok = obj.value("ok").toBool(false);
        QString msg = obj.value("msg").toString();
        emit registerResult(ok, msg);
        return;
    }

    if (type == "chat_ack")
    {
        // 服务器已存盘,客户端可以放心
        qint64 toUid = obj.value("to").toVariant().toLongLong();
        QString time = obj.value("time").toString();
        emit chatAck(toUid, time);
        return;
    }

    if (type == "chat_incoming")
    {
        qint64 from = obj.value("from").toVariant().toLongLong();
        QString msg = obj.value("msg").toString();
        QString t = obj.value("time").toString();
        emit chatIncoming(from, msg, t);
        return;
    }

    if (type == "history_reply")
    {
        QList<HistoryMsg> list;
        QJsonArray arr = obj.value("messages").toArray();
        for (const auto &v : arr)
        {
            QJsonObject o = v.toObject();
            HistoryMsg m;
            m.fromUid = o.value("from").toVariant().toLongLong();
            m.msg = o.value("msg").toString();
            m.time = o.value("time").toString();
            list.append(m);
        }
        qint64 peer = obj.value("peer_uid").toVariant().toLongLong();
        emit historyReceived(peer, list);
        return;
    }

    if (type == "files_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        if (!ok)
        {
            emit errorMessage(obj.value("msg").toString());
            return;
        }

        QList<FileEntry> list;
        QJsonArray arr = obj.value("files").toArray();
        for (const auto &v : arr)
        {
            QJsonObject f = v.toObject();
            FileEntry e;
            e.transferId = f.value("transfer_id").toString();
            e.fromUid = f.value("from").toVariant().toLongLong();
            e.filename = f.value("filename").toString();
            e.size = f.value("size").toVariant().toLongLong();
            e.time = f.value("time").toString();
            list.append(e);
        }
        qint64 peer = obj.value("peer_uid").toVariant().toLongLong();
        emit filesReceived(peer, list);
        return;
    }

    if (type == "download_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        const QString tid = obj.value("transfer_id").toString();

        if (!ok)
        {
            const QString msg = obj.value("msg").toString();
            qDebug() << "[MessageHandler] download_reply 失败:" << tid << msg;
            emit downloadFailed(tid, msg);
            return;
        }

        const QString filename = obj.value("filename").toString();
        const qint64 size = obj.value("size").toVariant().toLongLong();
        const qint64 fromUid = obj.value("from_uid").toVariant().toLongLong();

        qDebug() << "[MessageHandler] download_reply 成功:" << tid
                 << "filename=" << filename << "size=" << size;
        emit downloadReady(tid, filename, size, fromUid);
        return;
    }

    if (type == "delete_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        const QString tid = obj.value("transfer_id").toString();
        const QString msg = obj.value("msg").toString();
        emit deleteReply(ok, tid, ok ? QString() : msg);
        return;
    }

    if (type == "echo_reply")
    {
        const QString msg = obj.value("msg").toString();
        emit echoReceived(msg);
        return;
    }

    if (type == "login_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        if (ok)
        {
            emit loginResult(true, QString(), obj);
        }
        else
        {
            const QString msg = obj.value("msg").toString();
            emit loginResult(false, msg, obj);
        }
        return;
    }

    if (type == "search_user_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        qint64 uid = 0;
        QString nickname;
        bool online = false;
        QString state = "none";

        if (ok)
        {
            QJsonObject u = obj.value("user").toObject();
            uid = u.value("uid").toVariant().toLongLong();
            nickname = u.value("nickname").toString();
            online = u.value("online").toBool(false);
            state = obj.value("state").toString("none");
        }

        const QString msg = ok ? QString()
                               : obj.value("msg").toString();
        emit searchUserResult(uid, nickname, online, state, ok, msg);
        return;
    }

    if (type == "add_friend_reply")
    {
        const bool ok = obj.value("ok").toBool(false);
        const QString msg = obj.value("msg").toString();
        emit addFriendResult(ok, msg);
        return;
    }

    if (type == "friend_requests_reply")
    {
        QList<FriendRequestEntry> list;
        QJsonArray arr = obj.value("requests").toArray();
        for (const auto &v : arr)
        {
            QJsonObject o = v.toObject();
            FriendRequestEntry e;
            e.requestId = o.value("request_id").toString();
            e.fromUid = o.value("from_uid").toVariant().toLongLong();
            list.append(e);
        }
        emit friendRequestsReceived(list);
        return;
    }

    if (type == "friend_request_reply_ack")
    {
        const QString rid = obj.value("request_id").toString();
        const bool accepted = obj.value("accepted").toBool(false);
        const bool ok = obj.value("ok").toBool(false);
        const QString msg = obj.value("msg").toString();
        emit friendRequestReplyAck(rid, accepted, ok, msg);
        return;
    }

    if (type == "updateFriends")
    {
        // 服务器在双向好友关系生效后推这个,
        // 客户端收到后 widget 会调 sendGetFriends 拉新列表
        sendGetFriends(m_currentUid);
        return;
    }

    if (type == "error")
    {
        const QString msg = obj.value("msg").toString();
        emit errorMessage(msg);
        return;
    }

    qDebug() << "[MessageHandler] 未处理的 JSON type:" << type;
}
