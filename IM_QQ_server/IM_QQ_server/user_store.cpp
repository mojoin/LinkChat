// user_store.cpp: 用户数据访问层实现
#include "user_store.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

UserStore& UserStore::instance()
{
    static UserStore inst;
    return inst;
}

bool UserStore::loadFromFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "[UserStore] failed to open: " << path << "\n";
        return false;
    }

    json j;
    try {
        ifs >> j;
        path_ = path;
    } catch (const json::parse_error& e) {
        std::cerr << "[UserStore] parse error in " << path << ": " << e.what() << "\n";
        return false;
    }

    if (!j.is_array()) {
        std::cerr << "[UserStore] " << path << " is not a JSON array\n";
        return false;
    }

    std::unordered_map<int, User> next;
    next.reserve(j.size());

    for (const auto& item : j) {
        // 单条格式不对就跳过，不让一条坏数据毁掉整个文件
        if (!item.is_object()) continue;
        if (!item.contains("uid")      || !item["uid"].is_number_integer())      continue;
        if (!item.contains("password") || !item["password"].is_string())         continue;
        if (!item.contains("nickname") || !item["nickname"].is_string())         continue;

        User u;
        u.uid      = item["uid"].get<int>();
        u.password = item["password"].get<std::string>();
        u.nickname = item["nickname"].get<std::string>();
        next[u.uid] = std::move(u);
    }

    users_ = std::move(next);
    std::cerr << "[UserStore] loaded " << users_.size() << " users from " << path << "\n";
    return true;
}

std::optional<UserStore::User>
UserStore::verify(int uid, const std::string& password) const
{
    auto it = users_.find(uid);
    if (it == users_.end())            return std::nullopt;   // uid 不存在
    if (it->second.password != password) return std::nullopt; // 密码错
    return it->second;                                       // 成功，连 nickname 一起返回
}

std::optional<std::string>
UserStore::getNickname(int uid) const
{
    auto it = users_.find(uid);
    if (it == users_.end()) return std::nullopt;
    return it->second.nickname;
}

bool UserStore::addUser(int uid, const std::string &password, const std::string &nickname)
{
    if(users_.count(uid) > 0) return false;

    // 1. 内存加
    User u;
    u.uid      = uid;
    u.password = password;
    u.nickname = nickname;
    users_[uid] = u;

    // 2. 全量写回
    if(path_.empty()) return false;

    json arr = json::array();
    for (const auto& [_, user] : users_) {
        arr.push_back({
            {"uid",      user.uid},
            {"password", user.password},
            {"nickname", user.nickname}
        });
    }
    std::ofstream ofs(path_);
    if (!ofs.is_open()) return false;
    ofs << arr.dump(2);
    return true;
}
