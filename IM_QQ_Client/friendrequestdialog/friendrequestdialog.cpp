#include "friendrequestdialog.h"
#include "ui_friendrequestdialog.h"
#include "tcpclient/tcpclient.h"
#include "messagehandler/messagehandler.h"

#include <QTableWidgetItem>
#include <QHeaderView>
#include <QDebug>

FriendRequestDialog::FriendRequestDialog(TcpClient *client,
                                         MessageHandler *handler,
                                         QWidget *parent)
    : QDialog(parent),
      ui(new Ui::FriendRequestDialog),
      m_tcp(client),
      m_handler(handler)
{
    ui->setupUi(this);

    // 表格设置
    ui->tableWidget_Requests->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget_Requests->verticalHeader()->setVisible(false);
    ui->tableWidget_Requests->setColumnWidth(0, 100);
    ui->tableWidget_Requests->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 底部按钮初始禁用(没选中时不能用)
    ui->pB_Accept->setEnabled(false);
    ui->pB_Reject->setEnabled(false);

    // 网络回调
    if (m_handler)
    {
        connect(m_handler, &MessageHandler::friendRequestsReceived,
                this, &FriendRequestDialog::onRequestsReceived);
        connect(m_handler, &MessageHandler::friendRequestReplyAck,
                this, &FriendRequestDialog::onReplyAck);
    }

    // 进入即拉一次
    requestList();
}

FriendRequestDialog::~FriendRequestDialog()
{
    delete ui;
}

void FriendRequestDialog::on_pB_Refresh_clicked()
{
    requestList();
}

void FriendRequestDialog::on_pB_Accept_clicked()
{
    int row = ui->tableWidget_Requests->currentRow();
    if (row < 0 || row >= m_requests.size() || !m_handler)
        return;

    const auto &r = m_requests.at(row);
    setBusy(true, QStringLiteral("正在同意..."));
    m_handler->sendFriendRequestReply(r.requestId, true);
}

void FriendRequestDialog::on_pB_Reject_clicked()
{
    int row = ui->tableWidget_Requests->currentRow();
    if (row < 0 || row >= m_requests.size() || !m_handler)
        return;

    const auto &r = m_requests.at(row);
    setBusy(true, QStringLiteral("正在拒绝..."));
    m_handler->sendFriendRequestReply(r.requestId, false);
}

void FriendRequestDialog::on_pB_Close_clicked()
{
    reject();
}

void FriendRequestDialog::on_tableWidget_Requests_itemSelectionChanged()
{
    bool has = ui->tableWidget_Requests->currentRow() >= 0;
    ui->pB_Accept->setEnabled(has && !m_busy);
    ui->pB_Reject->setEnabled(has && !m_busy);
}

void FriendRequestDialog::requestList()
{
    if (!m_handler)
    {
        ui->label_Status->setText(QStringLiteral("网络未连接"));
        return;
    }
    setBusy(true, QStringLiteral("正在加载..."));
    m_handler->sendListFriendRequests();
}

void FriendRequestDialog::renderList()
{
    ui->tableWidget_Requests->setRowCount(0);
    ui->tableWidget_Requests->setRowCount(m_requests.size());

    for (int i = 0; i < m_requests.size(); ++i)
    {
        const auto &r = m_requests.at(i);

        auto *uidItem = new QTableWidgetItem(QStringLiteral("uid: %1").arg(r.fromUid));
        uidItem->setTextAlignment(Qt::AlignCenter);
        uidItem->setData(Qt::UserRole, r.requestId);

        ui->tableWidget_Requests->setItem(i, 0, uidItem);
    }

    ui->label_Status->setText(QStringLiteral("共 %1 条申请").arg(m_requests.size()));
    on_tableWidget_Requests_itemSelectionChanged();
}

void FriendRequestDialog::onRequestsReceived(
    const QList<MessageHandler::FriendRequestEntry> &requests)
{
    m_requests = requests;
    renderList();
    setBusy(false, QString());
}

void FriendRequestDialog::onReplyAck(const QString &request_id,
                                     bool accepted,
                                     bool ok,
                                     const QString &msg)
{
    if (accepted)
    {
        // 向服务器拉取好友列表
        m_handler->sendGetFriends(m_handler->currentUid());
    }
    
    if (!ok)
    {
        ui->label_Status->setText(QStringLiteral("操作失败:") + msg);
        setBusy(false, QString());
        return;
    }

    for (int i = 0; i < m_requests.size(); ++i)
    {
        if (m_requests.at(i).requestId == request_id)
        {
            m_requests.removeAt(i);
            break;
        }
    }
    renderList();
    setBusy(false, QStringLiteral("已处理"));
}

void FriendRequestDialog::setBusy(bool busy, const QString &status)
{
    m_busy = busy;
    ui->pB_Refresh->setEnabled(!busy);
    ui->tableWidget_Requests->setEnabled(!busy);
    on_tableWidget_Requests_itemSelectionChanged();
    if (!status.isEmpty())
        ui->label_Status->setText(status);
}
