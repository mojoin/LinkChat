#include "file_store.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string minMaxKey(int a, int b)
{
    int mn = std::min(a, b);
    int mx = std::max(a, b);
    return std::to_string(mn) + "_" + std::to_string(mx);
}

std::string FileStore::convDirFor(int uid_a, int uid_b)
{
    return "data/files/conv_" + minMaxKey(uid_a, uid_b);
}

std::string FileStore::manifestPathFor(int uid_a, int uid_b)
{
    return convDirFor(uid_a, uid_b) + "/manifest.jsonl";
}

std::string FileStore::savedPathFor(const TransferRecord &r)
{
    return convDirFor(r.from_uid, r.to_uid) + "/" + r.transfer_id + "__" + r.file_name;
}

std::string FileStore::tmpPathFor(const TransferRecord &r)
{
    return convDirFor(r.from_uid, r.to_uid) + "/" + r.transfer_id + ".tmp";
}

// 生成唯一 transfer id
std::string FileStore::genTransferId()
{
    auto now = std::chrono::system_clock::now();    // 时间点
    std::time_t tt = std::chrono::system_clock::to_time_t(now); // 秒级时间戳
    std::tm t = *std::localtime(&tt);

    std::ostringstream date;
    date << "tr_" << std::put_time(&t, "%Y%m%d") << "_";

    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream rand;
    rand << std::hex << std::setw(6) << std::setfill('0')
         << (dist(rng) & 0xFFFFFF);
    return date.str() + rand.str();
}

void FileStore::recordUploaded(const TransferRecord &r)
{
    fs::create_directories(convDirFor(r.from_uid, r.to_uid));

    json j;
    j["transfer_id"] = r.transfer_id;
    j["from_uid"] = r.from_uid;
    j["to_uid"] = r.to_uid;
    j["file_name"] = r.file_name;
    j["file_size"] = r.file_size;
    j["upload_time"] = r.upload_time;
    j["saved_name"] = r.saved_name;

    std::ofstream ofs(manifestPathFor(r.from_uid, r.to_uid), std::ios::app);
    ofs << j.dump() << "\n";
    ofs.close();
}

std::vector<TransferRecord> FileStore::listForPair(int uid_a, int uid_b)
{
    std::vector<TransferRecord> result;
    std::ifstream ifs(manifestPathFor(uid_a, uid_b));
    if (!ifs.is_open())
        return result;

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;
        try
        {
            auto j = json::parse(line);
            TransferRecord r;
            r.transfer_id = j["transfer_id"].get<std::string>();
            r.from_uid = j["from_uid"].get<int>();
            r.to_uid = j["to_uid"].get<int>();
            r.file_name = j["file_name"].get<std::string>();
            r.file_size = j["file_size"].get<int64_t>();
            r.upload_time = j.value("upload_time", "");
            r.saved_name = j.value("saved_name", "");
            result.push_back(r);
        }
        catch (...)
        { /* 跳过坏行 */
        }
    }
    return result;
}

static std::optional<TransferRecord> parseManifestLine(const std::string &line)
{
    try
    {
        auto j = json::parse(line);
        TransferRecord r;
        r.transfer_id = j["transfer_id"].get<std::string>();
        r.from_uid    = j["from_uid"].get<int>();
        r.to_uid      = j["to_uid"].get<int>();
        r.file_name   = j["file_name"].get<std::string>();
        r.file_size   = j["file_size"].get<int64_t>();
        r.upload_time = j.value("upload_time", "");
        r.saved_name  = j.value("saved_name", "");
        return r;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// 定向查找:已知对方 uid,直接读该对话的 manifest,不扫其他目录
std::optional<TransferRecord> FileStore::findByTransferId(int uid_a, int uid_b, const std::string &transfer_id)
{
    std::ifstream ifs(manifestPathFor(uid_a, uid_b));
    if (!ifs.is_open())
        return std::nullopt;

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;
        auto r = parseManifestLine(line);
        if (r && r->transfer_id == transfer_id)
            return r;
    }
    return std::nullopt;
}

// 兜底:没带 peer_uid 时全盘扫(平时不会走到)
std::optional<TransferRecord> FileStore::findByTransferId(const std::string &transfer_id)
{
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator("data/files", ec))
    {
        if (!entry.is_directory())
            continue;

        std::ifstream ifs((entry.path() / "manifest.jsonl").string());
        if (!ifs.is_open())
            continue;

        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.empty())
                continue;
            auto r = parseManifestLine(line);
            if (r && r->transfer_id == transfer_id)
                return r;
        }
    }
    return std::nullopt;
}

bool FileStore::deleteTransfer(int uid_a, int uid_b, const std::string &transfer_id)
{
    // 1. 定向查该对话的 manifest(拿到完整记录,同时确定磁盘路径)
    auto rec = findByTransferId(uid_a, uid_b, transfer_id);
    if (!rec)
        return false;

    // 2. 删磁盘文件 (路径 = convDir/<transfer_id>__<原文件名>)
    std::string file_path = savedPathFor(*rec);
    std::error_code ec;
    std::filesystem::remove(file_path, ec);
    if (ec)
        std::cerr << "deleteTransfer: remove file failed: " << ec.message() << std::endl;

    // 3. 重写 manifest.jsonl:只保留该对话对里除本条以外的记录
    auto remaining = listForPair(uid_a, uid_b);
    std::string manifest = manifestPathFor(uid_a, uid_b);
    std::ofstream ofs(manifest, std::ios::trunc);
    for (const auto &r : remaining)
    {
        if (r.transfer_id == transfer_id)
            continue;

        json j;
        j["transfer_id"] = r.transfer_id;
        j["from_uid"]    = r.from_uid;
        j["to_uid"]      = r.to_uid;
        j["file_name"]   = r.file_name;
        j["file_size"]   = r.file_size;
        j["upload_time"] = r.upload_time;
        j["saved_name"]  = r.saved_name;
        ofs << j.dump() << "\n";
    }
    ofs.close();

    return true;
}