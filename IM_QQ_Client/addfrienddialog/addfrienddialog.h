#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include <QDialog>
#include <QString>
#include <QtGlobal>

class TcpClient;
class MessageHandler;

namespace Ui
{
    class AddFriendDialog;
}

class AddFriendDialog : public QDialog
{
    Q_OBJECT

public:
    // client: 网络层(handler内部已持有,这里再传一份以便弹错误)
    // myUid: 当前登录用户的 uid(用于判断 state是不是自己→自己,这里不直接用,留给 handler校验)
    AddFriendDialog(TcpClient *client,
                             MessageHandler *handler,
                             QWidget *parent = nullptr);
    ~AddFriendDialog();

private slots:
    void on_pB_Search_clicked(); // 点击"搜索"
    void on_pB_Add_clicked();    // 点击"添加好友"
    void on_pB_Cancel_clicked(); // 关闭按钮

    // 来自 MessageHandler 的回调
    void onSearchResult(qint64 uid,
                        const QString &nickname,
                        bool online,
                        const QString &state,
                        bool ok,
                        const QString &msg);
    void onAddResult(bool ok, const QString &msg);

private:
    void setBusy(bool busy, const QString &status);

    Ui::AddFriendDialog *ui;

    TcpClient *m_tcp     = nullptr;
    MessageHandler *m_handler = nullptr;

    // 当前搜索到的用户(添加按钮要发给谁)
    qint64  m_searchedUid = 0;
    QString m_searchedState; // none / already_friend / pending_sent
};

#endif // ADDFRIENDDIALOG_H
