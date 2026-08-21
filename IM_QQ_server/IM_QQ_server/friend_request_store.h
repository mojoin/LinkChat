// friend_request_store.h: 好友申请记录数据访问层
//
// 职责：
//   从 data/friend_requests.json 加载好友申请到内存；
//   提供 append / hasPending /写回磁盘。
//
// 数据源格式（数组）：
//   [
//     {
//       "request_id": "frq_xxx",
//       "from_uid":   10001,
//       "to_uid":     10002,
//       "status":     "pending"
//     },
//     ...
//   ]
//
// 线程安全：
//   单例 + 同步写回（每次 append 后立刻 saveToFile）。
//   IM 服务器场景，单线程 epoll 事件循环足够。

#pragma once

#include <string>
#include <vector>
#include <optional>

class FriendRequestStore
{
public:
    struct Request
    {
        std::string request_id;
        int from_uid = 0;
        int to_uid = 0;
        std::string status; // pending / accepted / rejected（本次只产出 pending）
    };

    // 单例入口
    static FriendRequestStore &instance();

    // 从 JSON 加载。会清空已有数据，重新建索引。
    //   path    JSON 文件路径（不存在时视为空数组，不算失败）
    //   返回    true = 加载成功；false = 解析失败
    bool loadFromFile(const std::string &path);

    // 追加一条 pending 申请，自动写回磁盘，返回生成的 request_id
    std::string append(int from_uid, int to_uid);

    // 是否已存在 from -> to 的 pending 申请
    bool hasPending(int from_uid, int to_uid) const;

    // 列出 [to_uid] 收到的所有 pending 请求（list_friend_requests 用）
    std::vector<Request> listPendingFor(int to_uid) const;

    // 按 request_id 查找
    std::optional<Request> findById(const std::string &request_id) const;

    // 删除一条记录（按 request_id）。找到则删除并写回磁盘，返回 true；
    // 找不到返回 false（不写磁盘）。
    bool remove(const std::string &request_id);

    // 已加载的记录条数（调试 / 日志用）
    std::size_t size() const { return requests_.size(); }

private:
    FriendRequestStore() = default;
    FriendRequestStore(const FriendRequestStore &) = delete;
    FriendRequestStore &operator=(const FriendRequestStore &) = delete;

    // 把内存里的 requests_ 写回 path
    bool saveToFile() const;

    std::vector<Request> requests_;
    std::string path_;
};
