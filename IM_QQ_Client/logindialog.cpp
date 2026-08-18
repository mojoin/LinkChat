#include "logindialog.h"
#include "ui_logindialog.h"
#include "tcpclient/tcpclient.h"
#include "messagehandler/messagehandler.h"

#include <QIntValidator>
#include <registerdialog/registerdialog.h>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>

LoginDialog::LoginDialog(TcpClient *tcp, MessageHandler *handler, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog),
    m_tcp(tcp),
    m_handler(handler)
{
    ui->setupUi(this);
    // 设置为只能输入数字
    ui->lineEdit_Account->setValidator(new QIntValidator(0, 9999999999, this));
    // 初始禁用登录按钮，等握手完成
    ui->btn_LogIn->setEnabled(false);
    // ========== 订阅 MessageHandler 的业务信号 ==========
    // 握手完成 → 启用登录按钮
    connect(m_handler, &MessageHandler::handshakeDone, this, [this]() {
        ui->btn_LogIn->setEnabled(true);
    });

    connect(m_handler, &MessageHandler::loginResult, this,
            [this](bool ok, const QString &msg, const QJsonObject & /*extra*/)
            {
                ui->btn_LogIn->setEnabled(true);
                if (ok)
                {
                    // 登录成功:停掉超时 timer,关闭对话框
                    if (m_timeoutTimer)
                        m_timeoutTimer->stop(); // ← 加这行
                    accept();
                }
                else
                {
                    QMessageBox::warning(this, "登录失败", msg);
                }
            });

    // 登录结果
    connect(m_handler, &MessageHandler::loginResult, this,
        [this](bool ok, const QString &msg, const QJsonObject &/*extra*/) {
        ui->btn_LogIn->setEnabled(true);
        if (ok) {
            // 登录成功：关闭对话框，main 会接着创建 Widget
            accept();
        } else {
            QMessageBox::warning(this, "登录失败", msg);
        }
    });

    // 服务器推错误（error 帧、ERR_NEED_LOGIN 等）
    connect(m_handler, &MessageHandler::errorMessage, this,
        [this](const QString &msg) {
        QMessageBox::warning(this, "错误", msg);
    });
  
    // ========== 登录按钮 ==========
    connect(ui->btn_LogIn, &QPushButton::clicked, this, [this]() {
        const QString account = ui->lineEdit_Account->text().trimmed();
        const QString password = ui->lineEdit_Password->text();

        if (account.isEmpty() || password.isEmpty()) {
            QMessageBox::warning(this, "登录失败", "账号和密码不能为空");
            return;
        }

        bool ok = false;
        int uid = account.toInt(&ok);
        if (!ok) {
            QMessageBox::warning(this, "登录失败", "账号必须是数字");
            return;
        }

        // 禁用按钮，防止重复点
        ui->btn_LogIn->setEnabled(false);
        // 直接交给 MessageHandler
        m_handler->sendLogin(uid, password);
    });

    // ========== 注册按钮 ==========
    connect(ui->label_Register, &ClickableLabel::clicked, this, [this]() {
        RegisterDialog *regDlg = new RegisterDialog(m_handler, this);
        regDlg->move(this->pos());
        connect(regDlg, &RegisterDialog::backToLogin, this, [this, regDlg]() {
            regDlg->close();
            this->show();
        });
        connect(regDlg, &QDialog::finished, this, [this](int /*result*/) {
            this->show();
        });
        
        regDlg->show();
        //this->hide();
    });

    // ========== 300 秒不登录自动关闭 ==========
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);            // 只触发一次
    m_timeoutTimer->setInterval(300 * 1000);        // 300 秒    
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        reject(); 
    });
    m_timeoutTimer->start();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}
