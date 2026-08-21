// friend_store.h: 好友关系数据访问层
//
// 职责：
//   从 data/friends.json 加载好友关系到内存，提供 uid->好友列表 的查询。
//   只做查询（不写回），不做业务判断。
//
// 数据源格式（数组）：
//   [
//     { "uid": 10001, "friends": [10002, 10003] },
//     { "uid": 10002, "friends": [10001] },
//     ...
//   ]
//
// 线程安全：
//   单例 + 只在启动时 load 一次，运行时只读，IM 服务器场景足够。
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class FriendStore
{
public:
    struct Friend
    {
        int uid = 0;
        std::string nickname; // 联表查 UserStore 拿到
    };

    // 单例入口
    static FriendStore &instance();

    // 从 JSON 加载。会清空已有数据，重新建索引。
    //   path    JSON 文件路径
    //   返回    true = 加载成功（哪怕数组为空也算成功）；false = 失败
    bool loadFromFile(const std::string &path);

    // 取 uid 的好友列表（带昵称）
    //   返回    vector<Friend>：uid 不存在返回空 vector
    std::vector<Friend> getFriends(int uid) const;

    // 双向往 friends.json 加边：把 b 加到 a 的列表 + a 加到 b 的列表；写回磁盘
    //   已存在的边不重复加。返回 true = 写盘成功。
    bool addEdge(int a, int b);

    // 已加载的关系条数（调试 / 日志用）
    std::size_t size() const { return edges_.size(); }

private:
    FriendStore() = default;
    FriendStore(const FriendStore &) = delete;
    FriendStore &operator=(const FriendStore &) = delete;

    // 把内存里的 edges_ 写回 path
    bool saveToFile() const;

    std::unordered_map<int, std::vector<int>> edges_; // uid -> 好友uid列表
    std::string path_;
};
