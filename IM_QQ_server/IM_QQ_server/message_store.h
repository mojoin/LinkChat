#pragma once
#include <string>
#include <vector>

struct ChatMessage {
    int from_uid;
    int to_uid;
    std::string msg;
    std::string time;   // "2026-08-17 10:30:00"
};

class MessageStore {
public:
    // 追加一条消息(append 到 chat_<min>_<max>.json)
    static void appendMessage(const ChatMessage &m);

    // 读取两个用户之间的所有历史(读 chat_<min>_<max>.json过滤)
    static std::vector<ChatMessage> readHistory(int uid_a, int uid_b);
};