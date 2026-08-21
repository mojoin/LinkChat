#include "friend_request_store.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <random>
#include <cstdio>

using json = nlohmann::json;

namespace
{
    // 生成 "frq_" + 16 位 hex 的 request_id
    std::string genRequestId()
    {
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_int_distribution<uint64_t> dist;
        uint64_t v = dist(rng);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "frq_%016llx",
                      (unsigned long long)v);
        return std::string(buf);
    }
}

FriendRequestStore &FriendRequestStore::instance()
{
    static FriendRequestStore inst;
    return inst;
}

bool FriendRequestStore::loadFromFile(const std::string &path)
{
    // 记下路径,saveToFile 用
    path_ = path;

    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        // 首次运行文件不存在 → 视为空数组,不算失败
        std::cerr << "[FriendRequestStore] file not found, starting empty: "
                  << path << "\n";
        requests_.clear();
        return true;
    }

    json j;
    try
    {
        ifs >> j;
    }
    catch (const json::parse_error &e)
    {
        std::cerr << "[FriendRequestStore] parse error in " << path
                  << ": " << e.what() << "\n";
        return false;
    }

    if (!j.is_array())
    {
        std::cerr << "[FriendRequestStore] " << path
                  << " is not a JSON array\n";
        return false;
    }

    std::vector<Request> next;
    next.reserve(j.size());

    for (const auto &item : j)
    {
        if (!item.is_object())
            continue;
        if (!item.contains("request_id") || !item["request_id"].is_string())
            continue;
        if (!item.contains("from_uid") || !item["from_uid"].is_number_integer())
            continue;
        if (!item.contains("to_uid") || !item["to_uid"].is_number_integer())
            continue;
        if (!item.contains("status") || !item["status"].is_string())
            continue;

        Request r;
        r.request_id = item["request_id"].get<std::string>();
        r.from_uid = item["from_uid"].get<int>();
        r.to_uid = item["to_uid"].get<int>();
        r.status = item["status"].get<std::string>();
        next.push_back(std::move(r));
    }

    requests_ = std::move(next);
    std::cerr << "[FriendRequestStore] loaded " << requests_.size()
              << " requests from " << path << "\n";
    return true;
}

std::string FriendRequestStore::append(int from_uid, int to_uid)
{
    Request r;
    r.request_id = genRequestId();
    r.from_uid = from_uid;
    r.to_uid = to_uid;
    r.status = "pending";
    requests_.push_back(std::move(r));

    // 立刻落盘(单线程场景够用,失败打日志)
    if (!saveToFile())
    {
        std::cerr << "[FriendRequestStore] append: saveToFile failed\n";
    }
    return requests_.back().request_id;
}

bool FriendRequestStore::hasPending(int from_uid, int to_uid) const
{
    for (const auto &r : requests_)
    {
        if (r.status == "pending" && r.from_uid == from_uid && r.to_uid == to_uid)
        {
            return true;
        }
    }
    return false;
}

bool FriendRequestStore::saveToFile() const
{
    if (path_.empty())
    {
        std::cerr << "[FriendRequestStore] saveToFile: path not set\n";
        return false;
    }

    json arr = json::array();
    for (const auto &r : requests_)
    {
        arr.push_back({{"request_id", r.request_id},
                       {"from_uid", r.from_uid},
                       {"to_uid", r.to_uid},
                       {"status", r.status}});
    }

    std::ofstream ofs(path_);
    if (!ofs.is_open())
    {
        std::cerr << "[FriendRequestStore] saveToFile: failed to open "
                  << path_ << "\n";
        return false;
    }
    ofs << arr.dump(2); // 缩进 2,易读
    return true;
}

std::vector<FriendRequestStore::Request>
FriendRequestStore::listPendingFor(int to_uid) const
{
    std::vector<Request> out;
    for (const auto &r : requests_)
    {
        if (r.status == "pending" && r.to_uid == to_uid)
            out.push_back(r);
    }
    return out;
}

std::optional<FriendRequestStore::Request>
FriendRequestStore::findById(const std::string &request_id) const
{
    for (const auto &r : requests_)
    {
        if (r.request_id == request_id)
            return r;
    }
    return std::nullopt;
}

bool FriendRequestStore::remove(const std::string &request_id)
{
    for (auto it = requests_.begin(); it != requests_.end(); ++it)
    {
        if (it->request_id == request_id)
        {
            requests_.erase(it);
            if (!saveToFile())
            {
                std::cerr << "[FriendRequestStore] remove: saveToFile failed\n";
                return false;
            }
            return true;
        }
    }
    return false;
}
