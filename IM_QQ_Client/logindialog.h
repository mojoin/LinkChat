#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QTcpSocket>
#include <QTimer>

class TcpClient;
class MessageHandler;

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    // 接收外部传入的 TcpClient + MessageHandler
    explicit LoginDialog(TcpClient *tcp, MessageHandler *handler, QWidget *parent = nullptr);
    // 让外部（main / Widget）拿到同一个实例，复用同一条连接
    TcpClient* tcpClient() const { return m_tcp; }
    MessageHandler* messageHandler() const { return m_handler; }
    ~LoginDialog();

private:
    TcpClient *m_tcp = nullptr;// 不 delete，外部拥有
    MessageHandler *m_handler = nullptr;// 不 delete，外部拥有
    QTimer *m_timeoutTimer = nullptr;   // 界面300s不登录就关闭
    Ui::LoginDialog *ui;
};

#endif // LOGINDIALOG_H
