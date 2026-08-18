#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QKeyEvent>

class TcpClient;
class MessageHandler;

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(TcpClient *tcp, MessageHandler *handler, QWidget *parent = nullptr);
    ~Widget();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void on_pB_sendMessage_clicked();

private:
    void appendChatMessage(qint64 from_uid, const QString &text, const QString &time);

    qint64 m_currentPeerUid = 0; // 当前聊天对象 uid
    TcpClient *m_tcp          = nullptr;
    MessageHandler *m_handler = nullptr;
    Ui::Widget *ui;
};
#endif // WIDGET_H
