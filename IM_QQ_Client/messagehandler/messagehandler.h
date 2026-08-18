#ifndef MESSAGEHANDLER_H
#define MESSAGEHANDLER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QList>

class TcpClient;

class MessageHandler : public QObject
{
    Q_OBJECT
public:

    struct HistoryMsg {
        qint64 fromUid;
        QString msg;
        QString time;
    };

    struct FriendInfo {
        qint64 uid;
        QString nickname;
        bool online;
    };

    void sendChat(qint64 to_uid, const QString &msg);
    void sendGetHistory(qint64 peer_uid);

    // 当前登录用户 uid(登录后存起来)
    qint64 currentUid() const { return m_currentUid; }

    void sendGetFriends(qint64 uid);

    explicit MessageHandler(TcpClient *client, QObject *parent = nullptr);

    // === 客户端主动发送 ===
    void sendClientHello();
    void sendEcho(const QString &msg);
    void sendLogin(qint64 uid, const QString &password);
    void sendRegister(qint64 uid, const QString &password, const QString &nickname);

signals:
    // 收到一条别人发来的消息(在线时服务器转发过来)
    void chatIncoming(qint64 from_uid, const QString &msg, const QString &time);

    // 服务器确认 chat 已存盘(可选,目前客户端乐观显示不用)
    void chatAck(qint64 to_uid, const QString &time);

    // 拉取历史完成
    void historyReceived(qint64 peer_uid, const QList<HistoryMsg> &msgs);

    // 收到 friends_reply
    void friendsReceived(const QList<FriendInfo> &friends);

    // 握手完成：收到 SERVER_WELCOME 1 时触发
    void handshakeDone();

    // 收到 echo_reply：原样回显
    void echoReceived(const QString &msg);

    // 收到 login_reply：登录结果
    void loginResult(bool ok, const QString &msg, const QJsonObject &extra);

    // 注册结果(注册成功后由客户端自己决定是否自动登录)
    void registerResult(bool ok, const QString &msg);

    // 收到 error：协议层错误
    void errorMessage(const QString &msg);

    // 收到其他类型的消息（未处理）
    void serverReady();

private slots:
    void onFrame(const QString &line);

private:
    void handleJson(const QString &line);
    void handlePlain(const QString &line);
    TcpClient* m_tcp;
    bool m_handshaked = false; // 是否已完成握手（避免重复触发）
    qint64 m_currentUid = 0;  // 登录成功后存(用户id)
};

#endif // MESSAGEHANDLER_H
