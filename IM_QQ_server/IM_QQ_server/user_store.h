// user_store.h: 用户数据访问层
//
// 职责：
//   从 data/user.json 加载用户列表到内存，提供 uid->user 的查询。
//   只做查询（不写回），不做业务判断。
//
// 数据源格式（数组）：
//   [
//     { "uid": 10001, "password": "123456", "nickname": "小红" },
//     ...
//   ]
//
// 线程安全：
//   单例 + 只在启动时 load 一次，运行时只读，IM 服务器场景足够。
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

class UserStore {
public:
    struct User {
        int uid = 0;
        std::string password;
        std::string nickname;
    };

    // 单例入口（启动时调用 loadFromFile；之后 protocol 层通过 instance() 用）
    static UserStore& instance();

    // 从 JSON 文件加载。会清空已有数据，重新建索引。
    //   path    JSON 文件路径
    //   返回    true = 加载成功（哪怕数组为空也算成功）；false = 文件打不开 / 不是数组 / 解析失败
    bool loadFromFile(const std::string& path);

    // 校验 uid + password
    // 返回    optional<User>：成功返回带 nickname 的 User；uid 不存在或密码不对返回 nullopt
    std::optional<User> verify(int uid, const std::string& password) const;

    // 通过 uid 查 nickname（不校验密码，登录成功后的"打招呼"阶段用）
    // 返回    optional<string>：找到返回 nickname；找不到返回 nullopt
    std::optional<std::string> getNickname(int uid) const;

    // 已加载的用户数（调试 / 日志用）
    std::size_t size() const { return users_.size(); }

    // 添加新用户并写回磁盘(注册时调用)
    //  返回  true = 添加成功;false = uid 已存在
    bool addUser(int uid, const std::string &password, const std::string &nickname);

private:
    UserStore() = default;
    UserStore(const UserStore&)            = delete;
    UserStore& operator=(const UserStore&) = delete;

    // uid -> User
    std::unordered_map<int, User> users_;
    std::string path_;  // loadFromFile 时记录路径
};