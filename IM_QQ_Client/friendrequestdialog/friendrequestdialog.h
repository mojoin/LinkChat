#ifndef FRIENDREQUESTDIALOG_H
#define FRIENDREQUESTDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QtGlobal>

class TcpClient;
#include "messagehandler/messagehandler.h"
class QTableWidgetItem;

namespace Ui {
class FriendRequestDialog;
}

class FriendRequestDialog : public QDialog
{
    Q_OBJECT

public:
    FriendRequestDialog(TcpClient *client,
                        MessageHandler *handler,
                        QWidget *parent = nullptr);
    ~FriendRequestDialog();

private slots:
    void on_pB_Refresh_clicked();
    void on_pB_Accept_clicked();      // 底部"同意申请"
    void on_pB_Reject_clicked();      // 底部"拒绝"
    void on_pB_Close_clicked();

    // 来自 MessageHandler 的回调
    void onRequestsReceived(const QList<MessageHandler::FriendRequestEntry> &requests);
    void onReplyAck(const QString &request_id, bool accepted, bool ok, const QString &msg);

    // 表格选中变化
    void on_tableWidget_Requests_itemSelectionChanged();

private:
    void requestList();
    void renderList();
    void setBusy(bool busy, const QString &status);

    Ui::FriendRequestDialog *ui;

    TcpClient *m_tcp     = nullptr;
    MessageHandler *m_handler = nullptr;

    QList<MessageHandler::FriendRequestEntry> m_requests;
    bool m_busy = false;
};

#endif // FRIENDREQUESTDIALOG_H
