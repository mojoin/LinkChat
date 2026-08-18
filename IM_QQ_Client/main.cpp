#include "widget.h"
#include "logindialog.h"
#include "tcpclient/tcpclient.h"
#include "messagehandler/messagehandler.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ============共享一个 TcpClient + MessageHandler ============
    TcpClient *tcp          = new TcpClient(&a); // 父对象 a
    MessageHandler *handler = new MessageHandler(tcp, &a); // 父对象 a

    // 启动连接
    tcp->connectToServer("10.82.112.118", 9527);

    // ============ 登录对话框 ============
    LoginDialog loginDialog(tcp, handler);
    if (loginDialog.exec() == QDialog::Accepted) {
        // 登录成功 → 创建主窗口，复用同一个 tcp + handler
        Widget w(tcp, handler);
        w.show();
        return a.exec();
    }
}
