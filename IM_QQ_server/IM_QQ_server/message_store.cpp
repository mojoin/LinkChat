#include "message_store.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// 内部:计算 chat 文件路径(uid 小的在前面)
static std::string chatFileFor(int uid_a, int uid_b)
{
    int mn = std::min(uid_a, uid_b);
    int mx = std::max(uid_a, uid_b);
    return "data/chats/chat_" + std::to_string(mn) + "_" + std::to_string(mx) + ".json";
}

void MessageStore::appendMessage(const ChatMessage &m)
{
    std::string path = chatFileFor(m.from_uid, m.to_uid);
    // 确保文件路径存在
    fs::create_directories("data/chats");

    json j;
    j["from_uid"] = m.from_uid;
    j["to_uid"] = m.to_uid;
    j["msg"] = m.msg;
    j["time"] = m.time;

    std::ofstream ofs(path, std::ios::app);
    ofs << j.dump() << "\n";
    ofs.close();
}

std::vector<ChatMessage> MessageStore::readHistory(int uid_a, int uid_b)
{
    std::vector<ChatMessage> result;
    std::string path = chatFileFor(uid_a, uid_b);

    std::ifstream ifs(path);
    if (!ifs.is_open()) return result; // 文件不存在 → 空历史

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            ChatMessage m;
            m.from_uid = j["from_uid"].get<int>();
            m.to_uid   = j["to_uid"].get<int>();
            m.msg      = j["msg"].get<std::string>();
            m.time     = j["time"].get<std::string>();
            result.push_back(m);
        } catch (...) {
            // 跳过坏行(不破坏整个读取)
        }
    }
    return result;
}