#include "addfrienddialog.h"
#include "ui_addfrienddialog.h"
#include "tcpclient/tcpclient.h"
#include "messagehandler/messagehandler.h"

#include <QMessageBox>
#include <QIntValidator>
#include <QDebug>

// ============================================================
// 构造 / 析构
// ============================================================
AddFriendDialog::AddFriendDialog(TcpClient *client,
                                 MessageHandler *handler,
                                 QWidget *parent)
    : QDialog(parent), ui(new Ui::AddFriendDialog), m_tcp(client), m_handler(handler)
{
    ui->setupUi(this);

    // uid 输入框只接受整数
    ui->lineEdit_Uid->setValidator(new QIntValidator(1, 999999999, this));

    // 按钮槽
    // connect(ui->pB_Search, &QPushButton::clicked, this, &AddFriendDialog::on_pB_Search_clicked);
    // connect(ui->pB_Add, &QPushButton::clicked, this, &AddFriendDialog::on_pB_Add_clicked);
    // connect(ui->pB_Cancel, &QPushButton::clicked, this, &AddFriendDialog::on_pB_Cancel_clicked);

    // 回车 = 搜索
    connect(ui->lineEdit_Uid, &QLineEdit::returnPressed,
            this, &AddFriendDialog::on_pB_Search_clicked);

    // 来自 handler 的回调
    if (m_handler)
    {
        connect(m_handler, &MessageHandler::searchUserResult,
                this, &AddFriendDialog::onSearchResult);
        connect(m_handler, &MessageHandler::addFriendResult,
                this, &AddFriendDialog::onAddResult);
    }
}

AddFriendDialog::~AddFriendDialog()
{
    delete ui;
}

// ============================================================
// 搜索
// ============================================================
void AddFriendDialog::on_pB_Search_clicked()
{
    qDebug() << "1";
    if (!m_handler)
    {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("网络未连接"));
        return;
    }

    const QString s = ui->lineEdit_Uid->text().trimmed();
    if (s.isEmpty())
    {
        ui->label_Status->setText(QStringLiteral("请输入用户uid"));
        return;
    }

    bool ok = false;
    qint64 uid = s.toLongLong(&ok);
    if (!ok || uid <= 0)
    {
        ui->label_Status->setText(QStringLiteral("uid格式不合法"));
        return;
    }

    // 清掉上一次的结果
    m_searchedUid = 0;
    m_searchedState.clear();
    ui->label_Uid->setText("-");
    ui->label_Nickname->setText("-");
    ui->label_Online->setText("-");
    ui->pB_Add->setEnabled(false);

    setBusy(true, QStringLiteral("正在搜索 %1 ...").arg(uid));
    m_handler->sendSearchUser(uid);
}

// ============================================================
// 添加
// ============================================================
void AddFriendDialog::on_pB_Add_clicked()
{
    if (!m_handler || m_searchedUid == 0)
        return;

    setBusy(true, QStringLiteral("正在发送好友申请..."));
    m_handler->sendAddFriendRequest(m_searchedUid);
}

// ============================================================
// 关闭
// ============================================================
void AddFriendDialog::on_pB_Cancel_clicked()
{
    reject();
}

// ============================================================
// 搜索结果回调
// ============================================================
void AddFriendDialog::onSearchResult(qint64 uid,
                                     const QString &nickname,
                                     bool online,
                                     const QString &state,
                                     bool ok,
                                     const QString &msg)
{
    setBusy(false, QString()); // 清掉"正在搜索"状态条

    if(uid == m_handler->currentUid())
    {
        ui->label_Status->setText(QStringLiteral("不能添加自己"));
        return;
    }

    if (!ok)
    {
        ui->label_Uid->setText("-");
        ui->label_Nickname->setText("-");
        ui->label_Online->setText("-");
        ui->pB_Add->setEnabled(false);
        m_searchedUid = 0;
        m_searchedState.clear();
        ui->label_Status->setText(QStringLiteral("搜索失败:") + msg);
        return;
    }

    // 成功:填充 UI
    m_searchedUid = uid;
    m_searchedState = state;
    ui->label_Uid->setText(QString::number(uid));
    ui->label_Nickname->setText(nickname);
    ui->label_Online->setText(online ? QStringLiteral("● 在线")
                                     : QStringLiteral("○离线"));

    // 根据 state决定是否启用"添加好友"
    if (state == "none")
    {
        ui->pB_Add->setEnabled(true);
        ui->label_Status->setText(QStringLiteral("可以添加"));
    }
    else if (state == "already_friend")
    {
        ui->pB_Add->setEnabled(false);
        ui->label_Status->setText(QStringLiteral("对方已是你的好友"));
    }
    else if (state == "pending_sent")
    {
        ui->pB_Add->setEnabled(false);
        ui->label_Status->setText(QStringLiteral("已发送过申请,等待对方处理"));
    }
    else
    {
        ui->pB_Add->setEnabled(false); // 未知state,不保守起见启用
        ui->label_Status->setText(QStringLiteral("????"));
    }
}

// ============================================================
// 添加结果回调
// ============================================================
void AddFriendDialog::onAddResult(bool ok, const QString &msg)
{
    setBusy(false, QString());

    if (ok)
    {
        // 添加成功 → 把"添加"按钮禁用,状态改为已发送
        m_searchedState = "pending_sent";
        ui->pB_Add->setEnabled(false);
        ui->label_Status->setText(QStringLiteral("好友申请已发送,等待对方处理"));
    }
    else
    {
        ui->label_Status->setText(QStringLiteral("发送失败:") + msg);
    }
}

// ============================================================
// 内部:切换繁忙状态
// ============================================================
void AddFriendDialog::setBusy(bool busy, const QString &status)
{
    ui->pB_Search->setEnabled(!busy);
    ui->pB_Add->setEnabled(!busy && m_searchedUid != 0 && m_searchedState == "none");
    ui->lineEdit_Uid->setEnabled(!busy);

    if (!status.isEmpty())
        ui->label_Status->setText(status);
}