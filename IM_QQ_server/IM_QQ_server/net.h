#pragma once
#include <map>
#include <string>
#include <ctime>
#include <cstdint>
#include <fstream>
#include "IM_QQ_server.h" //  提供 socket_t 类型
#include <filesystem>

// net.h: 网络层
// 只负责 socket / epoll / recv / send，【不】负责任何业务。
// 目前的业务就是"原样回显"，所以这里什么也不暴露——只对外提供
// 一个启动服务器的总入口 startServer()。

// 启动 TCP 回显服务器，监听指定端口（默认 9527）
// 函数内部会一直循环，收到什么就原样 send 回客户端

enum ClientState{
    UNHANDSHAKED,   // 刚 accept,等 HELLO
    HANDSHAKED,     // HELLO 通过,等 LOGIN
    LOGGED_IN,      // 已登录
    KICKED          // 被踢,即将关闭
};

enum ConnMode {
    MODE_JSON,              // 默认, 按 \n 切 JSON 行
    MODE_BINARY_UPLOAD,     // 客户端正在上传文件字节
    MODE_BINARY_DOWNLOAD    // 服务器正在推文件字节给客户端
};

// ============ 每个 fd 的信息 ============
struct ClientInfo {
    ClientState state = UNHANDSHAKED;
    ConnMode    mode = MODE_JSON; // 套接字收发数据模式
    std::string account;          // 登录后填写
    time_t      connect_time;     // 超时判定用
};

// 文件传输临时状态 (仅 file mode 期间有值)
struct FileTransferState {
    std::string   transfer_id;
    int           from_uid       = 0;
    int           to_uid         = 0;
    std::string   file_name;
    std::string   saved_name;
    std::int64_t  file_size      = 0;
    std::int64_t  bytes_done     = 0;

    std::ofstream file_out;      // upload 端打开待写
    std::ifstream file_in;       // download 端打开待读

    // 帧解析状态: -1 = 正在等 4 字节长度头, 否则 = 当前块剩余字节数
    std::int32_t  expected_len  = -1;
    std::string   header_buf;    // 攒长度头
    std::string   chunk_buf;     // 攒一整块

    bool          eof_sent      = false;  // download 时是否已发 FIN
};

extern std::map<socket_t, ClientInfo> clients;    // fd → 状态
extern std::map<std::string, socket_t> user2fd;   // 反向索引:账号 → fd
extern std::map<socket_t, std::string> send_buffers;
extern std::map<socket_t, FileTransferState> file_state;

bool isUserOnline(int uid);
// 返回 fd 是否处于二进制模式(upload/download 中),此时不能塞 JSON
bool isFdBusy(int fd);
void startServer(int port = 9527);
// 根据 uid 查 fd(uid 在线时返回 fd,不在线返回 -1)
int fdOfUser(int uid);
// 给指定 fd 发一行 JSON(自动加 \n)。protocol 层用来转发 chat_incoming
void sendReplyToFd(int fd, const std::string &json);


// ============ 给 protocol.cpp 用的 mode 切换入口 (net.cpp 实现) ============
// 调用后, fd 立即进入 BINARY_UPLOAD 模式, 后续字节当作文件内容
void beginUpload(socket_t fd,
                 int from_uid,
                 int to_uid,
                 const std::string &transfer_id,
                 const std::string &file_name,
                 std::int64_t file_size,
                 const std::string &saved_name);

// 调用后, fd 进入 BINARY_DOWNLOAD 模式, EPOLLOUT 触发时服务器开始推字节
void beginDownload(socket_t fd, int from_uid, int peer_uid, const std::string &transfer_id);