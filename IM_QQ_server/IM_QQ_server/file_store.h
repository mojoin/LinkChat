#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct TransferRecord   // 上传(落盘)
{
    std::string transfer_id;
    int from_uid;
    int to_uid;
    std::string file_name;
    std::int64_t file_size;
    std::string upload_time;
    std::string saved_name; // 真实磁盘文件名 = transferId__原名
};

class FileStore
{
public:
    // 生成唯一 transfer id: "tr_YYYYMMDD_xxxxxx"
    static std::string genTransferId();

    // 计算一个对话对的目录路径 (uid 小的在前,跟 chat 一致)
    static std::string convDirFor(int uid_a, int uid_b);

    // 计算 manifest.jsonl 路径
    static std::string manifestPathFor(int uid_a, int uid_b);

    // 真实文件路径
    static std::string savedPathFor(const TransferRecord &r);

    // 临时文件路径 (.tmp 落盘用)
    static std::string tmpPathFor(const TransferRecord &r);

    // 上传完成后:append 一行到 manifest.jsonl
    static void recordUploaded(const TransferRecord &r);

    // 列出某对话对的所有传输记录
    static std::vector<TransferRecord> listForPair(int uid_a, int uid_b);

    // 通过 transfer_id 查找(下载时用)
    static TransferRecord *findByTransferId(const std::string &transfer_id);

    // 删除一条传输记录(双方都能删):删磁盘文件 + 移除 manifest +内存索引
    static bool deleteTransfer(const std::string &transfer_id);

};
