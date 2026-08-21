#include "sendfiledialog.h"
#include "ui_sendfiledialog.h"
#include "tcpclient/tcpclient.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>
#include <QCloseEvent>

SendFileDialog::SendFileDialog(TcpClient *client, qint64 toUid, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendFileDialog),
    m_tcp(client),
    m_toUid(toUid)
{
    ui->setupUi(this);

    ui->label_Title->setText(QStringLiteral("发送文件给好友"));
    //setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    // 监听服务器帧(upload_ready / error)与二进制发送进度
    if (m_tcp)
    {
        connect(m_tcp, &TcpClient::frameReceived, this, &SendFileDialog::onFrame);
        connect(m_tcp, &TcpClient::fileProgress, this, &SendFileDialog::onFileProgress);
        connect(m_tcp, &TcpClient::fileSendFinished, this, &SendFileDialog::onFileFinished);
        connect(m_tcp, &TcpClient::fileSendError, this, &SendFileDialog::onFileError);
    }
}

SendFileDialog::~SendFileDialog()
{
    delete ui;
}

void SendFileDialog::closeEvent(QCloseEvent *event)
{
    // 正在传输二进制文件时,请勿关闭
    if (m_transferring)
    {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void SendFileDialog::on_pB_Browse_clicked()
{
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
    if (path.isEmpty())
        return;

    QFileInfo info(path);
    m_filePath = path;
    m_fileSize = info.size();

    ui->lineEdit_FilePath->setText(path);
    ui->pB_Start->setEnabled(true);
    ui->progressBar->setValue(0);
    ui->label_Status->setText(
        QStringLiteral("已选择: %1  (%2) (%3 MB)")
            .arg(info.fileName())
            .arg(QString::number(m_fileSize))
            .arg(QString::number(m_fileSize / (1024.0 * 1024), 'f', 2)));
}

void SendFileDialog::on_pB_Start_clicked()
{
    if (m_filePath.isEmpty())
        return;
    if (!m_tcp || !m_tcp->isConnected())
    {
        ui->label_Status->setText(QStringLiteral("未连接服务器"));
        return;
    }

    // 置灰按钮,等待服务器确认
    setBusy(true, QStringLiteral("正在请求服务器..."));

    QJsonObject req;
    req["type"]     = "upload_file";
    req["to_uid"]   = m_toUid;
    req["filename"] = QFileInfo(m_filePath).fileName();
    req["size"]     = m_fileSize;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    m_tcp->sendFrame(QString::fromUtf8(line));
}

void SendFileDialog::on_pB_Cancel_clicked()
{
    if (m_transferring && m_tcp)
        m_tcp->cancelBinarySend();
    reject();
}

void SendFileDialog::onFrame(const QString &line)
{
    if (!line.startsWith('{'))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject())
        return;
    QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();

    if (type == "upload_ready")
    {
        const bool ok = obj.value("ok").toBool(false);
        if (ok)
        {
            m_transferId = obj.value("transfer_id").toString();
            ui->label_Status->setText(QStringLiteral("服务器已确认,开始传输..."));
            ui->progressBar->setValue(0);

            // 服务器已切到 BINARY_UPLOAD,开始分块发送
            m_transferring = true;
            m_tcp->sendBinaryFile(m_filePath);
        }
        else
        {
            ui->label_Status->setText(QStringLiteral("服务器拒绝: ") + obj.value("msg").toString());
            setBusy(false, ui->label_Status->text());
        }
        return;
    }

    if (type == "error")
    {
        ui->label_Status->setText(QStringLiteral("请求失败: ") + obj.value("msg").toString());
        setBusy(false, ui->label_Status->text());
        return;
    }
}

void SendFileDialog::onFileProgress(qint64 sent, qint64 total)
{
    if (total <= 0)
        return;
    ui->temp->setText(QStringLiteral("已上传: %1 / %2 字节")
                      .arg(sent).arg(total));

    int percent = (int)(sent * 100 / total);
    ui->progressBar->setValue(percent);
    ui->label_Status->setText(
        QStringLiteral("正在发送... %1 / %2 (%3%)")
            .arg(sent)
            .arg(total)
            .arg(percent));
}

void SendFileDialog::onFileFinished()
{
    m_transferring = false;
    ui->progressBar->setValue(100);
    ui->label_Status->setText(QStringLiteral("发送完成"));
    setBusy(false, ui->label_Status->text());
}

void SendFileDialog::onFileError(const QString &msg)
{
    m_transferring = false;
    ui->label_Status->setText(QStringLiteral("发送失败: ") + msg);
    setBusy(false, ui->label_Status->text());
}

void SendFileDialog::setBusy(bool busy, const QString &status)
{
    ui->pB_Start->setEnabled(!busy);
    ui->pB_Browse->setEnabled(!busy);
    ui->pB_Cancel->setEnabled(!busy);

    if (!status.isEmpty())
        ui->label_Status->setText(status);
}
