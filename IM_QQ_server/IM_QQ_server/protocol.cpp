// protocol.cpp: 协议层实现
//
// 通信规范（先约定死）：
//   客户端 -> 服务器：{"type":"<cmd>", ...}\n
//   服务器 -> 客户端：{"type":"<cmd>_reply" 或 "error", ...}\n
//
// 当前支持的命令：
//   echo        回显 msg 字段   {"type":"echo","msg":"hi"}
//                            -> {"type":"echo_reply","msg":"hi","ok":true}

#include "protocol.h"
#include "user_store.h"
#include "nlohmann/json.hpp"
#include "friend_store.h"
#include "net.h"
#include "message_store.h"
#include "file_store.h"

#include <string>

using namespace std;
using json = nlohmann::json;

namespace {

// 构造统一的错误回复（不带 \n）
string makeError(const string& msg)
{
    json j;
    j["type"] = "error";
    j["msg"]  = msg;
    j["ok"]   = false;
    return j.dump();
}

// echo 业务：原样回 msg
string handleEcho(const json& req)
{
    if (!req.contains("msg") || !req["msg"].is_string())
        return makeError("echo: missing or invalid 'msg' field");

    json reply;
    reply["type"] = "echo_reply";
    reply["msg"]  = req["msg"];      // 原样回
    reply["ok"]   = true;
    return reply.dump();
}

// 处理登录：用 uid + password 登录
// 客户端发：{"type":"login","uid":10001,"password":"123456"}
// 成功回：{"type":"login_reply","ok":true,"uid":10001,"nickname":"小红"}
// 失败回：{"type":"login_reply","ok":false,"msg":"wrong uid or password"}
// 协议层错误（字段缺失）：{"type":"error","msg":"...","ok":false}
string handleLogin(const json& req)
{
    // ① 字段校验：协议层错误，用 error type
    if (!req.contains("uid") || !req["uid"].is_number_integer())
        return makeError("login: missing or invalid 'uid' field");
    if (!req.contains("password") || !req["password"].is_string())
        return makeError("login: missing or invalid 'password' field");

    const int uid = req["uid"].get<int>();
    const string password = req["password"].get<string>();

    // ② 业务：用 UserStore 校验（数据源：data/user.json）
    auto user = UserStore::instance().verify(uid, password);
    const bool success = user.has_value();

    // ③ 业务结果：用对称回复（login_reply）
    json reply;
    reply["type"] = "login_reply";
    reply["ok"]   = success;
    if (success) {
        reply["uid"]      = uid;
        reply["nickname"] = user->nickname;
    } else {
        reply["msg"] = "wrong uid or password";
    }
    return reply.dump();
}

// 拉好友列表
string handleGetFriends(const json& req)
{
    if (!req.contains("uid") || !req["uid"].is_number_integer())
        return makeError("get_friends: missing or invalid 'uid' field");

    const int uid = req["uid"].get<int>();

    // 业务：先看这个 uid 存不存在（不存在 → 返回空列表而不是 error）
    auto friends = FriendStore::instance().getFriends(uid);

    json reply;
    reply["type"] = "friends_reply";
    reply["ok"]   = true;
    reply["uid"]  = uid;

    json arr = json::array();
    for (const auto& f : friends) {
        bool online = isUserOnline(f.uid);
        arr.push_back({{"uid", f.uid}, {"nickname", f.nickname}, {"online", online}});
    }
    reply["friends"] = arr;
    return reply.dump();
}

string handleRegister(const json& req) {
    if (!req.contains("uid") || !req["uid"].is_number_integer())
        return makeError("register: missing or invalid 'account' field");
    if (!req.contains("password") || !req["password"].is_string())
        return makeError("register: missing or invalid 'password' field");
    if (!req.contains("nickname") || !req["nickname"].is_string())
        return makeError("register: missing or invalid 'nickname' field");

    const int    uid  = req["uid"].get<int>();
    const string password = req["password"].get<string>();
    const string nickname = req["nickname"].get<string>();

    if (uid <= 0)
        return makeError("register: invalid account (must be > 0)");
    if (password.empty())
        return makeError("register: password cannot be empty");
    if (nickname.empty())
        return makeError("register: nickname cannot be empty");

    // 账号已存在
    auto existing = UserStore::instance().getNickname(uid);
    if (existing.has_value())
        return makeError("该账号已存在");

    // 添加用户(写回磁盘)
    bool added = UserStore::instance().addUser(uid, password, nickname);
    if (!added)
        return makeError("注册失败,请稍后重试"); 
        
    json reply;
    reply["type"] = "register_reply";
    reply["ok"]   = true;
    reply["uid"]  = uid;
    reply["msg"]  = "注册成功";
    return reply.dump();
}

// 发送聊天消息:append 到磁盘,在线则转发
string handleChat(const json& req, int from_uid)
{
    if (from_uid == 0)
        return makeError("chat: not logged in");
    if (!req.contains("to") || !req["to"].is_number_integer())
        return makeError("chat: missing or invalid 'to' field");
    if (!req.contains("msg") || !req["msg"].is_string())
        return makeError("chat: missing or invalid 'msg' field");

    const int    to   = req["to"].get<int>();
    const string msg  = req["msg"].get<string>();
    const string time = req.value("time", "");

    ChatMessage m;
    m.from_uid = from_uid;
    m.to_uid   = to;
    m.msg      = msg;
    m.time     = time;
    MessageStore::appendMessage(m);

    // 在线 → 转发 chat_incoming(不阻塞,失败由对方下次拉历史补救)
    int to_fd = fdOfUser(to);
    if (to_fd != -1) {
        json fwd;
        fwd["type"] = "chat_incoming";
        fwd["from"] = from_uid;
        fwd["msg"]  = msg;
        fwd["time"] = time;
        sendReplyToFd(to_fd, fwd.dump());
    }

    // 不回 chat_reply(客户端乐观显示),但回一个 ok 表示"服务器收到并已存盘"
    json reply;
    reply["type"] = "chat_ack";
    reply["ok"]   = true;
    reply["to"]   = to;
    reply["time"] = time;
    return reply.dump();
}

// 拉历史消息
string handleGetHistory(const json& req)
{
    if (!req.contains("uid") || !req["uid"].is_number_integer())
        return makeError("get_history: missing or invalid 'uid' field");
    if (!req.contains("peer_uid") || !req["peer_uid"].is_number_integer())
        return makeError("get_history: missing or invalid 'peer_uid' field");

    const int uid  = req["uid"].get<int>();
    const int peer = req["peer_uid"].get<int>();

    auto msgs = MessageStore::readHistory(uid, peer);
    json arr = json::array();
    for (const auto& m : msgs) {
        arr.push_back({
            {"from", m.from_uid},
            {"msg",  m.msg},
            {"time", m.time}
        });
    }
    json reply;
    reply["type"]     = "history_reply";
    reply["ok"]       = true;
    reply["peer_uid"] = peer;
    reply["messages"] = arr;
    return reply.dump();
}

// 开始上传:校验 → 生成 transfer_id → 切 MODE_BINARY_UPLOAD → 回 upload_ready
// 客户端发:{"type":"upload_file","to_uid":10002,"filename":"a.txt","size":1234}
// 服务器回:{"type":"upload_ready","ok":true,"transfer_id":"tr_...","filename":"a.txt","size":1234,"to_uid":10002}
string handleUploadFile(const json& req, int from_uid, int fd)
{
    if (from_uid == 0)
        return makeError("upload_file: not logged in");
    if (!req.contains("to_uid") || !req["to_uid"].is_number_integer())
        return makeError("upload_file: missing or invalid 'to_uid' field");
    if (!req.contains("filename") || !req["filename"].is_string())
        return makeError("upload_file: missing or invalid 'filename' field");
    if (!req.contains("size") || !req["size"].is_number_integer())
        return makeError("upload_file: missing or invalid 'size' field");

    const int    to_uid   = req["to_uid"].get<int>();
    const string filename = req["filename"].get<string>();
    const int64_t size    = req["size"].get<int64_t>();

    if (to_uid <= 0)
        return makeError("upload_file: invalid 'to_uid' (must be > 0)");
    if (filename.empty())
        return makeError("upload_file: filename cannot be empty");
    if (size < 0)
        return makeError("upload_file: invalid 'size' (must be >= 0)");

    // 生成唯一 transfer id,真实磁盘文件用 transfer_id 作为 saved_name
    const string transfer_id = FileStore::genTransferId();

    // 先切模式再回确认(与 download_file 对称):
    // 保证 handleMessage 返回时 mode 已切到 BINARY_UPLOAD,回包先于后续字节到达
    beginUpload(fd, from_uid, to_uid, transfer_id, filename, size, transfer_id);

    json reply;
    reply["type"]        = "upload_ready";
    reply["ok"]          = true;
    reply["transfer_id"] = transfer_id;
    reply["filename"]    = filename;
    reply["size"]        = size;
    reply["to_uid"]      = to_uid;
    return reply.dump();
}

// 列出 [我] 和 [peer_uid] 之间的所有文件
// 客户端发:{"type":"list_files","peer_uid":10002}
// 服务器回:{"type":"files_reply","ok":true,"peer_uid":10002,"files":[...]}
string handleListFiles(const json& req, int from_uid)
{
    if (from_uid == 0)
        return makeError("list_files: not logged in");
    if (!req.contains("peer_uid") || !req["peer_uid"].is_number_integer())
        return makeError("list_files: missing or invalid 'peer_uid' field");

    const int peer = req["peer_uid"].get<int>();

    // std::vector<TransferRecord> result;
    auto records = FileStore::listForPair(from_uid, peer);

    json arr = json::array();
    for (const auto &r : records)
    {
        // 防御性过滤:只保留 from_uid 参与的        
        if (r.from_uid != from_uid && r.to_uid != from_uid)
            continue;

        arr.push_back({
            {"transfer_id", r.transfer_id},
            {"from",         r.from_uid},
            {"filename",     r.file_name},
            {"size",         r.file_size},
            {"time",         r.upload_time}
        });
    }

    json reply;
    reply["type"]     = "files_reply";
    reply["ok"]       = true;
    reply["peer_uid"] = peer;
    reply["files"]    = arr;
    return reply.dump();
}

// 下载文件:校验权限 → 回 download_reply → 立刻 beginDownload 切二进制流
// 客户端发:{"type":"download_file","transfer_id":"tr_..."}
// 服务器回:{"type":"download_reply","ok":true,...} 然后直接切二进制流
string handleDownloadFile(const json& req, int from_uid, int fd)
{
    if (from_uid == 0)
        return makeError("download_file: not logged in");
    if (!req.contains("transfer_id") || !req["transfer_id"].is_string())
        return makeError("download_file: missing or invalid 'transfer_id' field");
    if (!req.contains("peer_uid") || !req["peer_uid"].is_number_integer())
        return makeError("download_file: missing or invalid 'peer_uid' field");

    const string tid = req["transfer_id"].get<string>();
    const int peer   = req["peer_uid"].get<int>();

    auto rec = FileStore::findByTransferId(from_uid, peer, tid);
    if (!rec)
    {   
        json reply;
        reply["type"]        = "download_reply";
        reply["ok"]          = false;
        reply["transfer_id"] = tid;
        reply["msg"]         = "transfer not found";
        return reply.dump();
    }

    // 权限校验:请求者必须是 owner 或 peer
    if (rec->from_uid != from_uid && rec->to_uid != from_uid)
    {
        json reply;
        reply["type"]        = "download_reply";
        reply["ok"]          = false;
        reply["transfer_id"] = tid;
        reply["msg"]         = "permission denied";
        return reply.dump();
    }

    // 成功:回元数据 JSON
    json reply;
    reply["type"]        = "download_reply";
    reply["ok"]          = true;
    reply["transfer_id"] = tid;
    reply["filename"]    = rec->file_name;
    reply["size"]        = rec->file_size;
    reply["from_uid"]    = rec->from_uid;

    // 立刻切二进制流:把 file_in 准备好,mode 切到 MODE_BINARY_DOWNLOAD
    // 接下来 net.cpp 的 processLine 把 reply send 出去,
    // 然后 EPOLLOUT 触发 flushSendBuf 自动按 32KB 推文件,发完追加 4字节0 (FIN)
    beginDownload(fd, from_uid, peer, tid);

    return reply.dump();
}

// 删除文件:双方都能删
// 客户端发:{"type":"delete_file","transfer_id":"tr_..."}
// 服务器回:{"type":"delete_reply","ok":true,"transfer_id":"tr_..."}
string handleDeleteFile(const json& req, int from_uid)
{
    if (from_uid == 0)
        return makeError("delete_file: not logged in");
    if (!req.contains("transfer_id") || !req["transfer_id"].is_string())
        return makeError("delete_file: missing or invalid 'transfer_id' field");

    const string tid = req["transfer_id"].get<string>();
    const int peer   = req["peer_uid"].get<int>();

    auto rec = FileStore::findByTransferId(from_uid, peer, tid);
    if (!rec)
    {
        json reply;
        reply["type"]        = "delete_reply";
        reply["ok"]          = false;
        reply["transfer_id"] = tid;
        reply["msg"]         = "transfer not found";
        return reply.dump();
    }

    // 双方都能删:owner 或 peer 任一即可
    if (rec->from_uid != from_uid && rec->to_uid != from_uid)
    {
        json reply;
        reply["type"]        = "delete_reply";
        reply["ok"]          = false;
        reply["transfer_id"] = tid;
        reply["msg"]         = "permission denied";
        return reply.dump();
    }

    bool deleted = FileStore::deleteTransfer(from_uid, peer, tid);

    json reply;
    reply["type"]        = "delete_reply";
    reply["ok"]          = deleted;
    reply["transfer_id"] = tid;
    if (!deleted)
        reply["msg"]     = "delete failed";
    return reply.dump();
}
}  // namespace

string handleMessage(const std::string &line, int from_uid, int fd)
{
    // 1. 解析 JSON
    json req;
    try {
        req = json::parse(line);    // 尝试把字符串变成 JSON 对象
    } catch (const json::parse_error& e) {
        return makeError(string("invalid json: ") + e.what());
    }

    // 2. 校验 type 字段
    if (!req.contains("type") || !req["type"].is_string())
        return makeError("missing or invalid 'type' field");

    const string type = req["type"].get<string>();

    // 3. 按 type 分发
    if (type == "echo")  return handleEcho(req);
    if (type == "login")  return handleLogin(req);
    if (type == "get_friends") return handleGetFriends(req);
    if (type == "register") return handleRegister(req);
    if (type == "chat") return handleChat(req, from_uid);
    if (type == "get_history") return handleGetHistory(req);
    if (type == "list_files")    return handleListFiles(req, from_uid);
    if (type == "upload_file")   return handleUploadFile(req, from_uid, fd);
    if (type == "download_file") return handleDownloadFile(req, from_uid, fd);
    if (type == "delete_file")   return handleDeleteFile(req, from_uid);

    return makeError("unknown type: " + type);
}