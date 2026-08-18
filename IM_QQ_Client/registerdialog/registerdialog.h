#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>

class MessageHandler;

namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegisterDialog(MessageHandler *handler, QWidget *parent = nullptr);

    ~RegisterDialog();

signals:
    void backToLogin(); // 返回登录信号

private:
    MessageHandler* m_handler = nullptr;
    Ui::RegisterDialog *ui;
};

#endif // REGISTERDIALOG_H
