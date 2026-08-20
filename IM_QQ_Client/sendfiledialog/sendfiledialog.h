#ifndef SENDFILEDIALOG_H
#define SENDFILEDIALOG_H

#include <QDialog>
#include <QString>
#include <QtGlobal>

class TcpClient;

namespace Ui {
class SendFileDialog;
}

class SendFileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendFileDialog(TcpClient *client, qint64 toUid, QWidget *parent = nullptr);
    ~SendFileDialog();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pB_Browse_clicked();
    void on_pB_Start_clicked();
    void on_pB_Cancel_clicked();

    // 来自 TcpClient 的回调
    void onFrame(const QString &line);
    void onFileProgress(qint64 sent, qint64 total);
    void onFileFinished();
    void onFileError(const QString &msg);

private:
    void setBusy(bool busy, const QString &status);

    Ui::SendFileDialog *ui;
    TcpClient *m_tcp = nullptr;   // 直接持有的网络层
    qint64     m_toUid = 0;       // 接收者 uid
    QString    m_filePath;        // 选中的文件路径
    qint64     m_fileSize = 0;    // 文件大小
    QString    m_transferId;      // 服务器分配的 transfer id
    bool       m_transferring = false; // 是否正在二进制上传
};

#endif // SENDFILEDIALOG_H
