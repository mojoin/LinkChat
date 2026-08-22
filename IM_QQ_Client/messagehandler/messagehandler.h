#ifndef MESSAGEHANDLER_H
#define MESSAGEHANDLER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>

class TcpClient;

class MessageHandler : public QObject
{
    Q_OBJECT
public:
    struct HistoryMsg
    {
        qint64 fromUid;
        QString msg;
        QString time;
    };

    struct FriendInfo
    {
        qint64 uid;
        QString nickname;
        bool online;
    };

    struct FileEntry
    {
        QString transferId;
        qint64 fromUid = 0;
        QString filename;
        qint64 size = 0;
        QString time;
    };

    struct FriendRequestEntry {
        QString requestId;
        qint64  fromUid = 0;
    };

    void sendChat(qint64 to_uid, const QString &msg);
    void sendGetHistory(qint64 peer_uid);

    // 当前登录用户 uid(登录后存起来)
    qint64 currentUid() const { return m_currentUid; }
    // 当前登录用户 nickname(登录后存起来)
    QString currentNickname() const { return m_currentNick; }

    // 好友相关
    void sendGetFriends(qint64 uid);
    void sendSearchUser(qint64 uid);
    void sendAddFriendRequest(qint64 to_uid);
    void sendListFriendRequests();
    void sendFriendRequestReply(const QString &request_id, bool accept);

    // 文件传输相关
    void sendListFiles(qint64 peer_uid);
    void sendDownloadFile(const QString &transfer_id, qint64 peer_uid);
    void sendDeleteFile(const QString &transfer_id, qint64 peer_uid);

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

    // 收到 search_user_reply:搜索结果
    // state 取值: "none" / "already_friend" / "pending_sent"
    void searchUserResult(qint64 uid, const QString &nickname, bool online, const QString &state, bool ok, const QString &msg);
    
    // 收到 add_friend_reply:添加好友结果
    void addFriendResult(bool ok, const QString &msg);

    // 收到 friend_requests_reply:加我的请求列表
    void friendRequestsReceived(const QList<FriendRequestEntry> &requests);

    // 收到 friend_request_reply_ack:我点同意/拒绝后,服务器告知处理完成
    // 客户端可以据此从申请列表里移掉这条
    void friendRequestReplyAck(const QString &request_id, bool accepted, bool ok, const QString &msg);

    // 注册结果(注册成功后由客户端自己决定是否自动登录)
    void registerResult(bool ok, const QString &msg);

    // 收到 files_reply:服务器返回了我与某好友的文件传输记录
    void filesReceived(qint64 peer_uid, const QList<FileEntry> &files);

    // 收到 download_reply ok=true:服务器准备好发文件,客户端可以切到二进制下载模式
    void downloadReady(const QString &transfer_id, const QString &filename,
                       qint64 size, qint64 from_uid);

    // 收到 download_reply ok=false:下载请求被拒绝
    void downloadFailed(const QString &transfer_id, const QString &msg);

    // 收到 delete_reply:删除文件成功
    void deleteReply(bool ok, const QString &transfer_id, const QString &msg);

    // 收到 error：协议层错误
    void errorMessage(const QString &msg);

    // 收到其他类型的消息（未处理）
    void serverReady();

private slots:
    void onFrame(const QString &line);

private:
    void handleJson(const QString &line);
    void handlePlain(const QString &line);
    TcpClient *m_tcp;
    bool m_handshaked = false; // 是否已完成握手（避免重复触发）
    qint64 m_currentUid = 0;   // 登录成功后存(用户id)
    QString m_currentNick;
};

#endif // MESSAGEHANDLER_H
