#include "widget.h"
#include "ui_widget.h"
#include <QDateTime>
#include <Qkeyevent>
#include "messagehandler/messagehandler.h"
#include "tcpclient/tcpclient.h"
#include <QDebug>

Widget::Widget(TcpClient *tcp, MessageHandler *handler, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_tcp(tcp)
    , m_handler(handler)
{
    ui->setupUi(this);

    // 让输入框+发送按钮所在行随底栏高度伸缩
    // ui->gridLayout->setRowStretch(0, 1);

    // 让 QTextEdit 行为像 QLineEdit
    ui->lineEidt_Message->setLineWrapMode(QTextEdit::NoWrap);  // 不自动折行
    // ui->lineEidt_Message->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 隐藏横滚动条

    // 收到在线消息(服务器转发)
    connect(m_handler, &MessageHandler::chatIncoming, this,
            [this](qint64 from_uid, const QString &msg, const QString &time)
            {
                // 当前聊天对象 == 发送方 → 直接追加;否则暂存(后续加未读计数)
                if (from_uid == m_currentPeerUid)
                {
                    appendChatMessage(from_uid, msg, time);
                }
                // TODO: else 暂存到未读队列
            });

    // 拉历史完成 →渲染整个聊天记录
    connect(m_handler, &MessageHandler::historyReceived, this,
            [this](qint64 peer_uid, const QList<MessageHandler::HistoryMsg> &msgs)
            {
                ui->message_view->clear();
                for (const auto &m : msgs)
                {
                    appendChatMessage(m.fromUid, m.msg, m.time);
                }
            });

    // 服务器确认(目前客户端乐观显示,这里留个空,以后可加重试逻辑)
    connect(m_handler, &MessageHandler::chatAck, this,
            [](qint64, const QString &) { /* OK */ });

    // 收到好友列表 → 渲染到 listWidget_Friends
    connect(m_handler, &MessageHandler::friendsReceived, this,
        [this](const QList<MessageHandler::FriendInfo> &friends) {
        ui->listWidget_Friends->clear();
        for (const auto &f : friends) {
        // 临时版本:用 ●/○ 字符前缀,以后用 delegate 美化
            QString marker = f.online ? "● " : "○ ";
            QString text = marker + f.nickname
                    + "  (uid:" + QString::number(f.uid) + ")";
            QListWidgetItem *item = new QListWidgetItem(text, ui->listWidget_Friends);
            item->setData(Qt::UserRole, f.uid);
        }
    });

    // 点击好友 → 切到该好友 + 拉历史
    connect(ui->listWidget_Friends, &QListWidget::itemClicked, this,
        [this](QListWidgetItem *item) {
        qint64 uid = item->data(Qt::UserRole).toLongLong();
        m_currentPeerUid = uid;

        // 显示好友名(去掉 ●/○ 前缀和 uid 后缀)
        QString raw = item->text();
        QString name = raw;
        int idx = name.indexOf(' ');
        if (idx >= 0) name = name.mid(idx + 1).trimmed();
        int paren = name.indexOf("  (uid:");
        if (paren >= 0) name = name.left(paren);
        ui->label_FriendName->setText(name);

        // 清空输入框
        ui->lineEidt_Message->clear();

        // 拉历史
        m_handler->sendGetHistory(uid);
    });


    // 向服务器拉取好友列表
    m_handler->sendGetFriends(m_handler->currentUid());
}

Widget::~Widget()
{
    delete ui;
}

void Widget::on_pB_sendMessage_clicked()
{
    if (m_currentPeerUid == 0) return; 
    QString text = ui->lineEidt_Message->toPlainText().trimmed();
    text.remove(QChar('\n'));   // 兜底：去掉所有换行
    text.remove(QChar('\r'));   // 兜底：去掉回车符
    if (text.isEmpty()) return; 
    
    // 1. 立即本地显示(乐观,不依赖服务器 ACK)
    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    appendChatMessage(m_handler->currentUid(), text, time);

    // 2. 发给服务器(服务器会 append + 在线转发)
    m_handler->sendChat(m_currentPeerUid, text);

    // 3. 清空输入框
    ui->lineEidt_Message->clear();
}

void Widget::appendChatMessage(qint64 from_uid, const QString &text, const QString &time)
{
    bool isMine = (from_uid == m_handler->currentUid());
    QString sender = isMine ? QStringLiteral("我") : QString::number(from_uid);
    QString align  = isMine ? "right" : "left";
    QString color  = isMine ? "#00aa00" : "#0066cc"; // 自己绿、对方蓝

    QString html = QString(
        // 间隔行(消息之间的间距)
        "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\">"
        "<tr><td height=\"8\"></td></tr></table>"
        // 消息(无背景色,只有对齐和文字色)
        "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\">"
        "<tr><td align=\"%1\" valign=\"top\">"
        "<font size=\"2\" color=\"#888888\">[%2] %3</font><br>"
        "<font color=\"%4\">%5</font>"
        "</td></tr></table>"
    ).arg(align, time, sender, color, text);

    ui->message_view->append(html);
}

void Widget::keyPressEvent(QKeyEvent *event)
{
        // 只在输入框有焦点时才拦截
    if (!ui->lineEidt_Message->hasFocus()) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        on_pB_sendMessage_clicked();
        return;   // 不调用基类 → QTextEdit 不会插入 \n
    }

    // 其它按键照常处理
    QWidget::keyPressEvent(event);
}