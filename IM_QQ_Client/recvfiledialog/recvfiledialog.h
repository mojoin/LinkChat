#ifndef RECVFILEDIALOG_H
#define RECVFILEDIALOG_H

#include <QDialog>
#include <QString>
#include <QList>
#include <QtGlobal>
#include "messagehandler/messagehandler.h" // 用 MessageHandler::FileEntry

class TcpClient;
class QCloseEvent;
class QTableWidgetItem;

namespace Ui {
class RecvFileDialog;
}

// 表格里一行的数据(直接复用 MessageHandler::FileEntry,字段一致)
using FileEntry = MessageHandler::FileEntry;

class RecvFileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecvFileDialog(TcpClient* client,
                            MessageHandler *handler,
                            qint64 myUid,
                            qint64 peerUid,
                            QWidget *parent = nullptr);                
    ~RecvFileDialog();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pB_Refresh_clicked();
    void on_pB_Download_clicked();
    void on_pB_Close_clicked();
    void onFilesReceived(qint64 peer_uid, const QList<FileEntry> &files);
    void onDownloadReady(const QString &transfer_id,
                                     const QString &filename,
                                     qint64 size,
                                     qint64 from_uid);

    void onDownloadFailed(const QString &transfer_id, const QString &msg);

    // 表格交互
    void on_tableWidget_Files_itemSelectionChanged();
    void on_tableWidget_Files_itemDoubleClicked(QTableWidgetItem *item);

    // 来自网络层
    // void onFrame(const QString &line);
    void onFileRecvProgress(qint64 received, qint64 total);
    void onFileRecvFinished(const QString &transferId, const QString &localPath);
    void onFileRecvError(const QString &msg);

private:
    void requestList(); // 发 {"type":"list_files", ...} 给服务器
    void renderList();  // 把 m_files 渲染到表格
    void resetTableSelection();
    void setBusy(bool busy, const QString &status);

    Ui::RecvFileDialog *ui;

    TcpClient *m_tcp     = nullptr;
    MessageHandler *m_handler = nullptr;
    qint64     m_myUid   = 0;     // 当前登录用户的 uid(用来判断方向)
    qint64     m_peerUid = 0;     // 正在查看的 peer uid

    // 下载相关状态:用户选了路径后暂存,等 download_reply ok 再启动 TcpClient
    QString m_pendingLocalPath;

    QList<FileEntry> m_files;
    bool    m_downloading = false; // 是否正在下载(用于关闭拦截)
};

#endif // RECVFILEDIALOG_H
