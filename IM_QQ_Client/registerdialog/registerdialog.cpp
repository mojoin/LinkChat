#include "registerdialog.h"
#include "ui_registerdialog.h"
#include "messagehandler/messagehandler.h"
#include <QIntValidator>
#include <QMessageBox>

RegisterDialog::RegisterDialog(MessageHandler *handler, QWidget *parent):
    QDialog(parent),
    ui(new Ui::RegisterDialog),
    m_handler(handler)
{
    ui->setupUi(this);
    setWindowTitle("注册新账号");

    ui->lineEdit_Account->setValidator(new QIntValidator(this));
    connect(ui->pushButton_backtologin, &QPushButton::clicked, this, [this](){
        emit backToLogin();
        this->close();
    });

    // 发起注册
    connect(ui->pushButton_register, &QPushButton::clicked, this, [this]() {
        if (!m_handler) return;

        QString uid  = ui->lineEdit_Account->text().trimmed();
        QString password = ui->lineEdit_Password->text();
        QString confirm  = ui->lineEdit_confirm_password->text();
        QString nickname = ui->lineEdit_nickname->text().trimmed();

        if (uid.isEmpty()) {
            QMessageBox::warning(this, "提示", "账号不能为空");
            return;
        }
        if (password.isEmpty()) {
            QMessageBox::warning(this, "提示", "密码不能为空");
            return;
        }
        if (password != confirm) {
            QMessageBox::warning(this, "提示", "两次密码不一致");
            return;
        }
        if (nickname.isEmpty()) {
            QMessageBox::warning(this, "提示", "昵称不能为空");
            return;
        }

        ui->pushButton_register->setEnabled(false);
        m_handler->sendRegister(uid.toLongLong(), password, nickname);
    });

    // 服务器回复
    connect(m_handler, &MessageHandler::registerResult, this,
        [this](bool ok, const QString &msg) {
        ui->pushButton_register->setEnabled(true);
        if (ok) {
            QMessageBox::information(this, "成功", "注册成功,请返回登录");
            emit backToLogin();
            this->close();
        } else {
            QMessageBox::warning(this, "注册失败", msg);
        }
    });
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}
