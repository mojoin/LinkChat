#include "friend_store.h"
#include "user_store.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

FriendStore& FriendStore::instance()
{
    static FriendStore inst;
    return inst;
}

bool FriendStore::loadFromFile(const std::string& path)
{
    // 记下路径,saveToFile 用
    path_ = path;

    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        // 文件不存在 → 视为空(首次运行),不算失败
        std::cerr << "[FriendStore] file not found, starting empty: " << path << "\n";
        edges_.clear();
        return true;
    }

    // 文件存在但为空 → 视为空数组
    ifs.seekg(0, std::ios::end);
    if (ifs.tellg() == 0)
    {
        std::cerr << "[FriendStore] file is empty, starting empty: " << path << "\n";
        edges_.clear();
        return true;
    }
    ifs.seekg(0, std::ios::beg);

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[FriendStore] parse error in " << path << ": " << e.what() << "\n";
        return false;
    }

    if (!j.is_array()) {
        std::cerr << "[FriendStore] " << path << " is not a JSON array\n";
        return false;
    }

    std::unordered_map<int, std::vector<int>> next;
    next.reserve(j.size());

    for (const auto& item : j) {
        if (!item.is_object()) continue;
        if (!item.contains("uid")      || !item["uid"].is_number_integer()) continue;
        if (!item.contains("friends")  || !item["friends"].is_array())      continue;

        const int uid = item["uid"].get<int>();
        std::vector<int> friends;
        friends.reserve(item["friends"].size());

        for (const auto& f : item["friends"]) {
            if (!f.is_number_integer()) continue;
            friends.push_back(f.get<int>());
        }
        next[uid] = std::move(friends);
    }

    edges_ = std::move(next);
    std::cerr << "[FriendStore] loaded " << edges_.size()
              << " entries from " << path << "\n";
    return true;
}

std::vector<FriendStore::Friend>
FriendStore::getFriends(int uid) const
{
    std::vector<Friend> result;

    auto it = edges_.find(uid);
    if (it == edges_.end()) return result;   // uid 不存在

    result.reserve(it->second.size());
    for (int fuid : it->second) {
        // 联表查 UserStore 拿昵称；找不到就跳过（用户可能已经注销）
        auto nick = UserStore::instance().getNickname(fuid);
        if (!nick.has_value()) continue;

        Friend f;
        f.uid      = fuid;
        f.nickname = *nick;
        result.push_back(std::move(f));
    }
    return result;
}

bool FriendStore::addEdge(int a, int b)
{
    if (a <= 0 || b <= 0 || a == b)
        return false;

    auto &va = edges_[a];
    if (std::find(va.begin(), va.end(), b) == va.end())
        va.push_back(b);

    auto &vb = edges_[b];
    if (std::find(vb.begin(), vb.end(), a) == vb.end())
        vb.push_back(a);

    if (!saveToFile())
    {
        std::cerr << "[FriendStore] addEdge: saveToFile failed\n";
        return false;
    }
    return true;
}

bool FriendStore::saveToFile() const
{
    if (path_.empty())
    {
        std::cerr << "[FriendStore] saveToFile: path not set\n";
        return false;
    }

    json arr = json::array();
    for (const auto &kv : edges_)
    {
        json friendsArr = json::array();
        for (int f : kv.second)
            friendsArr.push_back(f);
        arr.push_back({{"uid", kv.first}, {"friends", friendsArr}});
    }

    std::ofstream ofs(path_);
    if (!ofs.is_open())
    {
        std::cerr << "[FriendStore] saveToFile: failed to open " << path_ << "\n";
        return false;
    }
    ofs << arr.dump(2);
    return true;
}
