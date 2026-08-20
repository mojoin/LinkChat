#include "recvfiledialog.h"
#include "ui_recvfiledialog.h"
#include "tcpclient/tcpclient.h"
#include "messagehandler/messagehandler.h"

#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QDebug>

// ============================================================
// 构造 / 析构
// ============================================================
RecvFileDialog::RecvFileDialog (TcpClient *client,
                                MessageHandler *handler,
                                qint64 myUid,
                                qint64 peerUid,
                                QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RecvFileDialog)
    , m_tcp(client)
    , m_handler(handler)
    , m_myUid(myUid)
    , m_peerUid(peerUid)
{
    ui->setupUi(this);

    // 标题:与 uid:xxx 的文件传输记录
    ui->label_Title->setText(
        QStringLiteral("与 uid:%1 的文件传输记录").arg(m_peerUid));

    // 表格初始设置
    ui->tableWidget_Files->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget_Files->verticalHeader()->setVisible(false);
    ui->tableWidget_Files->setColumnWidth(0, 60);    // 方向
    ui->tableWidget_Files->setColumnWidth(1, 240);   // 文件名
    ui->tableWidget_Files->setColumnWidth(2, 100);   // 大小

    // 按钮槽
    connect(ui->pB_Refresh,  &QPushButton::clicked, this, &RecvFileDialog::on_pB_Refresh_clicked);
    // connect(ui->pB_Download, &QPushButton::clicked, this, &RecvFileDialog::on_pB_Download_clicked);
    connect(ui->pB_Close,    &QPushButton::clicked, this, &RecvFileDialog::on_pB_Close_clicked);

    // 表格信号
    connect(ui->tableWidget_Files, &QTableWidget::itemSelectionChanged,
            this, &RecvFileDialog::on_tableWidget_Files_itemSelectionChanged);
    connect(ui->tableWidget_Files, &QTableWidget::itemDoubleClicked,
            this, &RecvFileDialog::on_tableWidget_Files_itemDoubleClicked);

    // 网络信号 
    if (m_tcp) {
        // connect(m_tcp, &TcpClient::frameReceived,   this, &RecvFileDialog::onFrame);
        connect(m_tcp, &TcpClient::fileRecvProgress, this, &RecvFileDialog::onFileRecvProgress);
        connect(m_tcp, &TcpClient::fileRecvFinished, this, &RecvFileDialog::onFileRecvFinished);
        connect(m_tcp, &TcpClient::fileRecvError,    this, &RecvFileDialog::onFileRecvError);
    }

    // 被处理的网络消息
    if (m_handler)
    {
        connect(m_handler, &MessageHandler::filesReceived, this, &RecvFileDialog::onFilesReceived);
        connect(m_handler, &MessageHandler::downloadReady, this, &RecvFileDialog::onDownloadReady);
        connect(m_handler, &MessageHandler::downloadFailed, this, &RecvFileDialog::onDownloadFailed);
        connect(m_handler, &MessageHandler::deleteReply, this, &RecvFileDialog::onDeleteReply);
    }

    ui->label_Status->setText(QStringLiteral("正在加载..."));
    ui->progressBar->setVisible(false);
    ui->progressBar->setValue(0);

    // 打开对话框立即发 list_files
    requestList();
}

RecvFileDialog::~RecvFileDialog()
{
    delete ui;
}

// ============================================================
// 关闭拦截:下载中不让关
// ============================================================
void RecvFileDialog::closeEvent(QCloseEvent *event)
{
    if (m_downloading)
    {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

// ============================================================
// 按钮槽
// ============================================================
void RecvFileDialog::on_pB_Refresh_clicked()
{
    requestList();
}

void RecvFileDialog::on_pB_Download_clicked()
{
    if (m_downloading)
        return;

    int row = ui->tableWidget_Files->currentRow();
    if (row < 0 || row >= m_files.size())
        return;

    const FileEntry &f = m_files.at(row);

    // 默认文件名:用服务器给的原文件名
    QString defaultName = f.filename;

    // 默认保存路径:当前用户的"下载"目录
    QString defaultPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存文件到..."),
        QDir::homePath() + "/Downloads/" + defaultName);

    if (defaultPath.isEmpty())
        return;  // 用户取消了

    if (!m_handler) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("网络未连接"));
        return;
    }

    m_pendingLocalPath = defaultPath;
    m_handler->sendDownloadFile(f.transferId, m_peerUid);
    setBusy(true, QStringLiteral("等待服务器响应... (%1)").arg(f.filename));
}

void RecvFileDialog::on_pB_Close_clicked()
{
    reject();
}

void RecvFileDialog::on_pB_Delete_clicked()
{
    if (m_downloading) return;

    int row = ui->tableWidget_Files->currentRow();
    if (row < 0 || row >= m_files.size()) return;

    const FileEntry &f = m_files.at(row);

    auto btn = QMessageBox::question(this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定要删除与 uid:%1 的传输记录:\n%2 (%4 字节)\n\n服务器端文件和传输记录将被删除,本地已下载的副本不受影响。")
            .arg(m_peerUid).arg(f.filename).arg(f.size),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    if (!m_handler) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("网络未连接"));
        return;
    }

    m_handler->sendDeleteFile(f.transferId, m_peerUid);
    setBusy(true, QStringLiteral("正在删除 %1 ...").arg(f.filename));
}

void RecvFileDialog::onFilesReceived(qint64 peer_uid, const QList<FileEntry> &files)
{
    Q_UNUSED(peer_uid);
    m_files = files;
    renderList();
}

void RecvFileDialog::onDownloadReady(const QString &transfer_id,
                                     const QString &filename,
                                     qint64 size,
                                     qint64 from_uid)
{
    Q_UNUSED(from_uid);

    if (m_pendingLocalPath.isEmpty())
    {
        qDebug() << "[RecvFileDialog] onDownloadReady 但没有暂存路径";
        setBusy(false, QStringLiteral("下载失败:路径丢失"));
        return;
    }

    if (!m_tcp)
    {
        setBusy(false, QStringLiteral("下载失败:网络未连接"));
        m_pendingLocalPath.clear();
        return;
    }

    bool ok = m_tcp->startBinaryRecv(transfer_id, m_pendingLocalPath, size);
    if (!ok)
    {
        setBusy(false, QStringLiteral("下载失败:启动接收失败"));
        m_pendingLocalPath.clear();
        return;
    }

    // startBinaryRecv 成功,进入接收状态
    setBusy(true, QStringLiteral("正在下载 %1 (%2 字节)").arg(filename).arg(size));
}

void RecvFileDialog::onDownloadFailed(const QString &transfer_id, const QString &msg)
{
    Q_UNUSED(transfer_id);
    setBusy(false, QStringLiteral("下载失败:") + msg);
    m_pendingLocalPath.clear();
}


// ============================================================
// 表格交互
// ============================================================
void RecvFileDialog::on_tableWidget_Files_itemSelectionChanged()
{
    bool has = ui->tableWidget_Files->currentRow() >= 0;
    ui->pB_Download->setEnabled(has && !m_downloading);
    ui->pB_Delete->setEnabled(has && !m_downloading);
}

void RecvFileDialog::on_tableWidget_Files_itemDoubleClicked(QTableWidgetItem *item)
{
    Q_UNUSED(item);
    if (ui->pB_Download->isEnabled())
        on_pB_Download_clicked();
}

// ============================================================
// 网络回调(下一阶段填)
// ============================================================
// void RecvFileDialog::onFrame(const QString &line)
// {
//     Q_UNUSED(line);
//     // TODO: 解析 files_reply / download_reply / error
// }

void RecvFileDialog::onFileRecvProgress(qint64 received, qint64 total)
{
    if (total > 0)
    {
        int pct = static_cast<int>(received * 100 / total);
        ui->progressBar->setValue(pct);
        ui->label_Status->setText(
            QStringLiteral("下载中 %1 / %2 字节 (%3%%)")
                .arg(received).arg(total).arg(pct));
    }
}

void RecvFileDialog::onFileRecvFinished(const QString &transferId, const QString &localPath)
{
    Q_UNUSED(transferId);
    setBusy(false, QStringLiteral("下载完成"));
    m_pendingLocalPath.clear();

    QMessageBox::information(this,
                             QStringLiteral("下载完成"),
                             QStringLiteral("已保存到:\n%1").arg(localPath));
}

void RecvFileDialog::onFileRecvError(const QString &msg)
{
    setBusy(false, QStringLiteral("下载出错:") + msg);
    m_pendingLocalPath.clear();
}

void RecvFileDialog::onDeleteReply(bool ok, const QString &transfer_id, const QString &msg)
{
    if (ok)
    {
        setBusy(false, QStringLiteral("已删除"));
        // 从本地列表移除,不等服务器再回一次 list_files
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (m_files.at(i).transferId == transfer_id)
            {
                m_files.removeAt(i);
                break;
            }
        }
        renderList();
    }
    else
    {
        setBusy(false, QStringLiteral("删除失败"));
        QMessageBox::warning(this, QStringLiteral("删除失败"), msg);
    }
}

// ============================================================
// 内部函数
// ============================================================
void RecvFileDialog::requestList()
{
    ui->label_Status->setText(QStringLiteral("正在加载..."));

    if (m_handler) {
        m_handler->sendListFiles(m_peerUid);
        return;
    } else {
        // 兜底:网络未连接
        m_files.clear();
        renderList();
        ui->label_Status->setText(QStringLiteral("网络未连接"));
    }
    m_files.clear();

    renderList();
}

void RecvFileDialog::renderList()
{
    ui->tableWidget_Files->setRowCount(0);
    ui->tableWidget_Files->setRowCount(m_files.size());

    for (int i = 0; i < m_files.size(); ++i)
    {
        const FileEntry &f = m_files.at(i);

        // 方向:fromUid 等于我 → 是我发的;否则是对方发的
        QString dir = (f.fromUid == m_myUid)
                          ? QStringLiteral("↑ 发")
                          : QStringLiteral("↓ 收");

        // 大小格式化
        QString sizeStr;
        if (f.size < 1024)
            sizeStr = QStringLiteral("%1 B").arg(f.size);
        else if (f.size < 1024 * 1024)
            sizeStr = QStringLiteral("%1 KB").arg(f.size / 1024.0, 0, 'f', 1);
        else if (f.size < (qint64)1024 * 1024 * 1024)
            sizeStr = QStringLiteral("%1 MB").arg(f.size / (1024.0 * 1024), 0, 'f', 1);
        else
            sizeStr = QStringLiteral("%1 GB").arg(f.size / (1024.0 * 1024 * 1024), 0, 'f', 2);

        auto *dirItem  = new QTableWidgetItem(dir);
        auto *nameItem = new QTableWidgetItem(f.filename);
        auto *sizeItem = new QTableWidgetItem(sizeStr);
        auto *timeItem = new QTableWidgetItem(f.time);

        dirItem->setTextAlignment(Qt::AlignCenter);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // transfer_id 藏到第 0 列的 UserRole,下载时取出来
        dirItem->setData(Qt::UserRole, f.transferId);

        ui->tableWidget_Files->setItem(i, 0, dirItem);
        ui->tableWidget_Files->setItem(i, 1, nameItem);
        ui->tableWidget_Files->setItem(i, 2, sizeItem);
        ui->tableWidget_Files->setItem(i, 3, timeItem);
    }

    ui->label_Status->setText(QStringLiteral("共 %1 条记录").arg(m_files.size()));
    resetTableSelection();
}

void RecvFileDialog::resetTableSelection()
{
    ui->tableWidget_Files->clearSelection();
    ui->tableWidget_Files->setCurrentCell(-1, -1);
}

void RecvFileDialog::setBusy(bool busy, const QString &status)
{
    m_downloading = busy;
    ui->pB_Refresh->setEnabled(!busy);
    ui->pB_Download->setEnabled(!busy && ui->tableWidget_Files->currentRow() >= 0);
    ui->pB_Close->setEnabled(!busy);
    ui->tableWidget_Files->setEnabled(!busy);
    ui->progressBar->setVisible(busy);
    ui->pB_Delete->setEnabled(!busy && ui->tableWidget_Files->currentRow() >= 0);

    if (busy)
    {
        ui->progressBar->setValue(0);
        ui->label_Status->setText(status);
    }
    else if (!status.isEmpty())
    {
        ui->label_Status->setText(status);
    }
}
