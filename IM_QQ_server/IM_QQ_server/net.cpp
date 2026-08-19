#include "net.h"
#include "IM_QQ_server.h"
#include "protocol.h"
#include <utility>  // pair
#include "file_store.h"

#ifdef _WIN32
    // Windows 上没有 epoll，目前的服务器代码直接 #error 提醒
    // （你之后要么换 IOCP，要么用第三方库；这里保持原行为）
    #include <sys/epoll.h>   // 占位：实际不可用，保留原作者注释
#else
    #include <sys/epoll.h>
#endif

#include <cstring>   // strerror
#include "nlohmann/json.hpp"
using json = nlohmann::json;

using namespace std;

namespace{
    const char* SERVER_HELLO  = "SERVER_HELLO 1\n";      // 服务端发这个
    const char* CLIENT_HELLO  = "CLIENT_HELLO 1";      // 客户端发这个
    const char* SERVER_WELCOME = "SERVER_WELCOME 1\n";   // 服务端回这个
    const std::string LOGIN_OK_PREFIX  = "LOGIN_OK";
    const std::string LOGOUT_OK_PREFIX = "LOGOUT_OK";
    const std::string ERR_NEED_LOGIN   = "ERR_NEED_LOGIN\n";

    constexpr int HANDSHAKE_TIMEOUT = 5;   // 秒
    constexpr int LOGIN_TIMEOUT     = 300;  // 秒
}

char buf[4096];
map<socket_t, ClientInfo> clients;    // fd → 状态
map<string, socket_t> user2fd;   // 反向索引
map<socket_t, string> send_buffers;  // fd → 待发送数据缓冲区
map<socket_t, string> recv_buffers;  // fd → 待接收数据缓冲区
map<socket_t, FileTransferState> file_state;

// 前向声明（定义在文件后面）
static void processBinaryUpload(socket_t fd, const char* data, int n);
static void finalizeUpload(socket_t fd);
static void cleanupClient(socket_t fd, int epoll_fd, map<socket_t, string>& recv_buffers);


int fdOfUser(int uid)
{
    auto it = user2fd.find(to_string(uid));
    if (it == user2fd.end())
        return -1;
    return it->second;
}

bool isUserOnline(int uid)
{
    return user2fd.count(to_string(uid)) > 0;
}

static int trySend(socket_t fd, const string& buf){
    ssize_t n = send(fd, buf.data(), buf.size(), 0);
    if(n == (ssize_t)buf.size()){
        return 0;   // 全部发送成功
    };
    if(n > 0){
        send_buffers[fd] += buf.substr(n);
        return 1;   
    }
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
        send_buffers[fd] += buf;
        return 1;   // 发送缓冲区满，已缓存
    }
    return -1;
}

void sendReplyToFd(int fd, const string &json)
{
    if (fd < 0) return;
    trySend(fd, json + "\n");
}

static void flushSendBuf(socket_t fd, int epoll_fd){

    auto it_st = file_state.find(fd);
    if (it_st != file_state.end() && it_st->second.eof_sent
        && send_buffers.find(fd) == send_buffers.end())
    {
        file_state.erase(it_st);
        cout << "fd=" << fd << " download fully done, state cleared" << endl;
    }

    if (clients[fd].mode == MODE_BINARY_DOWNLOAD)
    {
        constexpr int CHUNK = 32 * 1024;    //32 KB
        char sendbuf[CHUNK + 4];    // 4字节头 + 32KB 数据
        auto& st = file_state[fd];

        if(!st.eof_sent)
        {
            // 读一块文件
            st.file_in.read(sendbuf + 4, CHUNK);
            int got = (int)st.file_in.gcount(); // 上次读了多少字节
            *(uint32_t*)sendbuf = htonl((uint32_t)got); // 小端转大端(网络)
            std::string frame(sendbuf, got + 4);

            int sr = trySend(fd, frame);
            if (sr < 0)
            {
                cleanupClient(fd, epoll_fd, recv_buffers);
                return;
            }

            st.bytes_done += got;

            if (sr == 1)
            {
                // send 缓冲区满, 等下次 EPOLLOUT
                struct epoll_event cev;
                cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                if (send_buffers.find(fd) != send_buffers.end())
                    cev.events |= EPOLLOUT;
                cev.data.fd = fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
                return;
            }

            // sr == 0: 已成功送出
            if (got == 0)
            {
                // 文件读完, 发 FIN (4 字节 0)
                uint32_t zero = 0;
                std::string fin((char*)&zero, 4);
                int sr2 = trySend(fd, fin);
                st.eof_sent = true;
                st.file_in.close();                       // 立刻关,不再读
                clients[fd].mode = MODE_JSON;             // 客户端下次发 JSON 能正常处理
                cout << "fd=" << fd << " download sent FIN, transfer=" << st.transfer_id << endl;
                // 不直接 return, 让下面继续把 send_buffers 兜底清空
            }
        } 
    }

    auto it = send_buffers.find(fd);
    if(it == send_buffers.end()) return;
    
    ssize_t n = send(fd, it->second.data(), it->second.size(), 0);
    if(n > 0){
        it->second.erase(0, n);
        if(it->second.empty())
            send_buffers.erase(it);
    }
    else if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK){
        cerr << "send() failed for fd=" << fd << "errno:" << errno << endl; 
    }

    struct epoll_event cev;
    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (send_buffers.find(fd) != send_buffers.end())
        cev.events |= EPOLLOUT;
    cev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
}

static void cleanupClient(socket_t fd, int epoll_fd,
                          map<socket_t, string> &recv_buffers)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    closesocket(fd);
    recv_buffers.erase(fd);
    send_buffers.erase(fd);

    auto it = file_state.find(fd);
	if (it != file_state.end())
    {
        if (it->second.file_out.is_open()) it->second.file_out.close();
        if (it->second.file_in.is_open())  it->second.file_in.close();
        // upload 半截断了: 删 .tmp
        if (!it->second.transfer_id.empty()
            && clients[fd].mode == MODE_BINARY_UPLOAD)
        {
            std::error_code ec;
            std::filesystem::remove(
                FileStore::convDirFor(it->second.from_uid, it->second.to_uid)
                + "/" + it->second.transfer_id + ".tmp", ec);
        }
        file_state.erase(it);
    }
    
    auto cit = clients.find(fd);
    if (cit != clients.end())
    {
        if (!cit->second.account.empty())
            user2fd.erase(cit->second.account);
        clients.erase(cit);
    }
}

static void sweepTimeouts(int epoll_fd,
                          map<socket_t, string> &recv_buffers)
{
    time_t now = time(nullptr);
    for (auto it = clients.begin(); it != clients.end(); )
    {
        const ClientInfo &ci = it->second;
        bool expired = false;
        if (ci.state == UNHANDSHAKED && (now - ci.connect_time) > HANDSHAKE_TIMEOUT)
            expired = true;
        else if (ci.state == HANDSHAKED && (now - ci.connect_time) > LOGIN_TIMEOUT)
            expired = true;
        
        if (expired)
        {
            socket_t fd = it->first;
            cerr << "fd=" << fd << " state=" << ci.state << " timeout" << endl;
            cleanupClient(fd, epoll_fd, recv_buffers);
            it = clients.begin();   // 重新开始,因为迭代器失效
            continue;
        }
        ++it;
    }
}

static bool processLine(socket_t fd, const string &line, int epoll_fd,
                        map<socket_t, string> &recv_buffers)
{
    ClientInfo &info = clients[fd];
    
    // ===== 状态 1:UNHANDSHAKED,只允许 CLIENT_HELLO =====
    if (info.state == UNHANDSHAKED)
    {
        if (line == CLIENT_HELLO)
        {
            info.state = HANDSHAKED;
            cout << "fd=" << fd << " handshake OK" << endl;
            
            int sr = trySend(fd, SERVER_WELCOME);
            if (sr == 1)
            {
                struct epoll_event cev;
                cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
                cev.data.fd = fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
            }
        }
        else    // "已读乱回"的客户端,直接踢掉
        {
            cerr << "fd=" << fd << " bad handshake: " << line << endl;
            cleanupClient(fd, epoll_fd, recv_buffers);
            return false;
        }
        return true;
    }
    
    // ===== 状态 2:HANDSHAKED,只允许 LOGIN 或 REGISTER =====
    if (info.state == HANDSHAKED)
    {
		bool isJsonCommand = (!line.empty() && line.front() == '{');
		if (!isJsonCommand)
		{
			int sr = trySend(fd, ERR_NEED_LOGIN);
			if (sr == 1)
			{
				struct epoll_event cev;
				cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
				cev.data.fd = fd;
				epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
			}
			return true;
		}
    }

    int from_uid = (clients[fd].account.empty()) ? 0 : stoi(clients[fd].account);
    string reply = handleMessage(line, from_uid, (int)fd);

    
	if (info.state == HANDSHAKED)
	{
		bool login_ok = false;
		string account;

		try {
			auto j = json::parse(reply);
			if (j.value("type", "") == "login_reply"
				&& j.value("ok", false) == true) {
				login_ok = true;
				account = to_string(j.value("uid", 0));
			}
		}
		catch (...) {}

		if (login_ok)
		{
			info.state = LOGGED_IN;
			if (!account.empty()) {
				info.account = account;
				user2fd[info.account] = fd;
			}
			cout << "fd=" << fd << " logged in as " << info.account << endl;
		}
	}
    
    // 登出 -> 回到 HANDSHAKED
    if (reply.rfind("LOGOUT_OK", 0) == 0)
    {
        user2fd.erase(info.account);
        info.account.clear();
        info.state = HANDSHAKED;
    }
    
    // 回包
    string full_reply = reply + "\n";
    int sr = trySend(fd, full_reply);
    if (sr == 1)
    {
        struct epoll_event cev;
        cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
        cev.data.fd = fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
    }
    else if (sr < 0)
    {
        cleanupClient(fd, epoll_fd, recv_buffers);
        return false;
    }
    
    return true;
}

void startServer(int port)
{
    // ============ 1. 创建监听 socket ============
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0); // TCP 协议
    if (sock == INVALID_SOCKET)
    {
        cerr << "socket() failed: " << errno << endl;
        return;
    }

    // ============ 2. 端口复用（解决 TIME_WAIT 残留） ============
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
    {
        cerr << "setsockopt(SO_REUSEADDR) failed: " << errno << endl;
        closesocket(sock);
        return;
    }

    // ============ 3. bind ============
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网卡
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        cerr << "bind() failed: " << errno << endl;
        closesocket(sock);
        return;
    }

    // ============ 4. listen ============
    if (listen(sock, 5) == SOCKET_ERROR)
    {
        cerr << "listen() failed: " << errno << endl;
        closesocket(sock);
        return;
    }

    // ============ 5. 把监听 socket 设为非阻塞（ET 模式必需） ============
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        cerr << "fcntl(O_NONBLOCK) failed: " << errno << endl;
        closesocket(sock);
        return;
    }

    // ============ 6. 创建 epoll 实例 ============
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1)
    {
        cerr << "epoll_create1() failed: " << errno << endl;
        closesocket(sock);
        return;
    }

    // ============ 7. 把监听 socket 注册进 epoll（边缘触发） ============
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // 边缘触发，只在状态变化时通知一次
    ev.data.fd = sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) == -1)
    {
        cerr << "epoll_ctl(ADD sock) failed: " << errno << endl;
        closesocket(epoll_fd);
        closesocket(sock);
        return;
    }

    cout << "Server listening on port " << port << " (epoll ET mode)" << endl;

    // ============ 8. epoll 主循环 ============
    struct epoll_event events[64];

    while (true)
    {
        int nready = epoll_wait(epoll_fd, events, 64, 1000); // 1 秒超时
        sweepTimeouts(epoll_fd, recv_buffers); // 扫描超时客户端
        if (nready == -1)
        {
            if (errno == EINTR) continue; // 信号打断，重试
            cerr << "epoll_wait() failed: " << errno << endl;
            break;
        }

        for (int i = 0; i < nready; i++)
        {
            socket_t fd = events[i].data.fd;

            // ---------- 情况 1：监听 socket 有新连接 ----------
            if (fd == sock)
            {
                // ET 模式：必须循环 accept 直到返回 EAGAIN
                while (true)
                {
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    socket_t client_sock = accept(sock,
                                                  (struct sockaddr *)&client_addr,
                                                  &client_addr_len);
                    if (client_sock == INVALID_SOCKET)
                    {
                        // EAGAIN / EWOULDBLOCK 表示已读完所有待处理连接
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        cerr << "accept() failed: " << errno << endl;
                        break;
                    }

                    // 把客户端 socket 也设为非阻塞
                    int cflags = fcntl(client_sock, F_GETFL, 0);
                    if (cflags == -1 || fcntl(client_sock, F_SETFL, cflags | O_NONBLOCK) == -1)
                    {
                        cerr << "fcntl(client O_NONBLOCK) failed: " << errno << endl;
                        cleanupClient(client_sock, epoll_fd, recv_buffers);
                        continue;
                    }

                    cout << "New client connected: "
                         << inet_ntoa(client_addr.sin_addr)
                         << ":" << ntohs(client_addr.sin_port)
                         << " fd=" << client_sock << endl;

                    // 初始化状态机: 刚 accept,默认就是 UNHANDSHAKED
                    clients[client_sock] = ClientInfo{};
                    clients[client_sock].connect_time = time(nullptr);

                    // 发送来自服务端的问候请求
                    int sr = trySend(client_sock, SERVER_HELLO);
                    if (sr < 0){
                        cerr << "send() failed for fd=" << client_sock << " errno:" << errno << endl;
                        closesocket(client_sock);
                        continue;
                    }

                    // 把客户端 socket 注册进 epoll（ET 模式）
                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // EPOLLRDHUP 监听对端关闭
                    if (sr > 0) cev.events |= EPOLLOUT; // 如果发送缓冲区满，注册可写事件
                    cev.data.fd = client_sock;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &cev) == -1)
                    {
                        cerr << "epoll_ctl(ADD client) failed: " << errno << endl;
                        cleanupClient(client_sock, epoll_fd, recv_buffers);
                        continue;
                    }
                }
            }
            // ---------- 情况 2：客户端 socket 有数据 / 断开 ----------
            else
            {
                // ===== 2.0:EPOLLOUT 触发,先把积压数据送出去 =====
				if (events[i].events & EPOLLOUT)
				{
					flushSendBuf(fd, epoll_fd);
					// 注意:EPOLLOUT 常和 EPOLLIN 同帧触发,不要 continue
					if (!(events[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP)))
						continue;
				}

                // ===== 2.1:对端关闭 =====
				if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
				{
					cout << "Client fd=" << fd << " disconnected (event=0x"
						<< hex << events[i].events << dec << ")" << endl;
					cleanupClient(fd, epoll_fd, recv_buffers);
					continue;
				}

                // ===== 2.2:收数据 =====
                if (events[i].events & EPOLLIN)
                {
                    if (clients[fd].mode == MODE_BINARY_UPLOAD) 
                    {
                        // ========== 二进制上传模式: 直接喂给 file_state ==========    
                        while(true)
                        {
                            int n = recv(fd, buf, sizeof(buf), 0);
                            if (n > 0)
                            {
                                processBinaryUpload(fd, buf, n);
                            }
                            else if (n == 0)
                            {
                                // 客户端断开
                                cout << "Upload fd=" << fd << " disconnected" << endl;
                                cleanupClient(fd, epoll_fd, recv_buffers);
                                break;
                            }
                            else  // n < 0
                            {
                                if (errno == EAGAIN || errno == EWOULDBLOCK)
                                    break;            // ← 内核空了就停, 下次 ET 会再触发
                                if (errno == EINTR)
                                    continue;
                                cerr << "recv() error upload fd=" << fd << endl;
                                cleanupClient(fd, epoll_fd, recv_buffers);
                                break;
                            }
                        }
                    }
                    else
					{
						while (true)
						{
							int n = recv(fd, buf, sizeof(buf), 0);
							if (n > 0)
							{
								recv_buffers[fd].append(buf, n); // 累积数据到缓冲区
								bool client_alive = true;
								size_t pos;
								while (client_alive &&
									(pos = recv_buffers[fd].find('\n')) != string::npos)
								{
									string line = recv_buffers[fd].substr(0, pos);
									recv_buffers[fd].erase(0, pos + 1);

									cout << "Received from fd=" << fd << ": " << line << endl;
									client_alive = processLine(fd, line, epoll_fd, recv_buffers);
									if (!client_alive) break;
									// // ===== ③ 交给协议层处理 =====
									// string reply = handleMessage(line);

									// // ===== ④ 回给客户端（手动补 \n） =====
									// send(fd, reply.data(), reply.size(), 0);
									// send(fd, "\n", 1, 0);
								}
							}
							else if (n == 0)
							{
								// 对端正常关闭（发送了 FIN）
								cout << "Client fd=" << fd << " closed." << endl;
								cleanupClient(fd, epoll_fd, recv_buffers);
								break;
							}
							else
							{
								// n < 0
								if (errno == EAGAIN || errno == EWOULDBLOCK)
								{
									// 数据已读完，正常情况
									break;
								}
								if (errno == EINTR) continue; // 信号打断，重试

								cerr << "recv() error on fd=" << fd
									<< ": " << errno << endl;
								cleanupClient(fd, epoll_fd, recv_buffers);
								break;
							}
						}
					}
                }
            }
        }
    }

    // ============ 9. 清理资源 ============
    closesocket(epoll_fd);
    closesocket(sock);
    cout << "Server shutdown." << endl;
}

// ============================================================
// 文件传输相关函数
// ============================================================
#include <cstring>   // memcpy
#include <arpa/inet.h>   // htonl, ntohl (Linux)

static int64_t parseI64(const json &j, const char *k, int64_t def = 0)
{
    return j.contains(k) ? j[k].get<int64_t>() : def;
}

// 协议层调用: 把 fd 切到 MODE_BINARY_UPLOAD, 准备 .tmp 文件
void beginUpload(socket_t fd, int from_uid, int to_uid,
                 const std::string &transfer_id,
                 const std::string &file_name,
                 int64_t file_size,
                 const std::string &saved_name)
{
    auto &st = file_state[fd];
    st.transfer_id = transfer_id;
    st.from_uid    = from_uid;
    st.to_uid      = to_uid;
    st.file_name   = file_name;
    st.saved_name  = saved_name;
    st.file_size   = file_size;
    st.bytes_done  = 0;
    st.expected_len = -1;
    st.header_buf.clear();
    st.chunk_buf.clear();
    st.eof_sent = false;

    // 临时文件路径 = 数据目录/transfer_id.tmp
    std::string tmp_path = FileStore::convDirFor(from_uid, to_uid) + "/"
                         + transfer_id + ".tmp";
    std::filesystem::create_directories(
        FileStore::convDirFor(from_uid, to_uid));
    st.file_out.open(tmp_path, std::ios::binary | std::ios::trunc);

    clients[fd].mode = MODE_BINARY_UPLOAD;
    cout << "fd=" << fd << " BINARY_UPLOAD start, transfer=" << transfer_id << endl;
}

// 解析一帧长度前缀 + 数据 (套接字, 接收buf数据, 数据长度)
static void processBinaryUpload(socket_t fd, const char *data, int n)
{
    auto &st = file_state[fd];
    int pos = 0;

    while (pos < n)
    {
        if (st.expected_len < 0)
        {
            // 攒 4 字节长度头
            int need = 4 - (int)st.header_buf.size();
            int take = std::min(need, n - pos);
            st.header_buf.append(data + pos, take);
            pos += take;

            if (st.header_buf.size() == 4)
            {
                uint32_t len;
                memcpy(&len, st.header_buf.data(), 4);
                int32_t L = (int32_t)ntohl(len);
                st.expected_len = L;
                cout << __FILE__ << __FILE__ << "后面多少是我的:" << L << endl;
                st.chunk_buf.clear();

                if (L == 0)
                {
                    // 收到 FIN
                    cout << "lcllcl";
                    finalizeUpload(fd);
                    return;
                }
            }
        }
        else
        {
            // 攒数据到一整块
            int need = st.expected_len - (int)st.chunk_buf.size();
            int take = std::min(need, n - pos);
            st.chunk_buf.append(data + pos, take);
            pos += take;

            if ((int)st.chunk_buf.size() == st.expected_len)
            {
                // 一块收完 → 写盘
                st.file_out.write(st.chunk_buf.data(), st.chunk_buf.size());
                cout << st.chunk_buf.data() << endl;
                st.bytes_done += st.chunk_buf.size();
                st.chunk_buf.clear();
                st.expected_len = -1;
            }
        }
    }
}

// 上传收完: rename, 写 manifest, 通知 B
static void finalizeUpload(socket_t fd)
{
    auto &st = file_state[fd];
    st.file_out.close();

    // .tmp → final
    std::string tmp_path = FileStore::convDirFor(st.from_uid, st.to_uid) + "/"
                         + st.transfer_id + ".tmp";
    std::string final_path = FileStore::savedPathFor({
        st.transfer_id, st.from_uid, st.to_uid,
        st.file_name, st.file_size, "", st.saved_name
    });
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        cerr << "rename failed: " << ec.message() << endl;
    }

    // 写 manifest
    char timebuf[32];
    time_t now = time(nullptr);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    TransferRecord r;
    r.transfer_id = st.transfer_id;
    r.from_uid    = st.from_uid;
    r.to_uid      = st.to_uid;
    r.file_name   = st.file_name;
    r.file_size   = st.file_size;
    r.upload_time = timebuf;
    r.saved_name  = st.saved_name;
    FileStore::recordUploaded(r);

    // 通知 B
    int to_fd = fdOfUser(st.to_uid);
    if (to_fd != -1)
    {
        json j;
        j["type"]        = "file_ready";
        j["transfer_id"] = st.transfer_id;
        j["file_name"]   = st.file_name;
        j["file_size"]   = st.file_size;
        j["saved_name"]  = st.saved_name;
        j["from_uid"]    = st.from_uid;
        sendReplyToFd(to_fd, j.dump());
    }

    cout << "fd=" << fd << " upload done, transfer=" << st.transfer_id << endl;

    file_state.erase(fd);
    clients[fd].mode = MODE_JSON;
}

// 协议层调用: 把 fd 切到 MODE_BINARY_DOWNLOAD
void beginDownload(socket_t fd, const std::string &transfer_id)
{
    TransferRecord *rec = FileStore::findByTransferId(transfer_id);
    if (!rec)
    {
        cerr << "beginDownload: not found " << transfer_id << endl;
        // 走原本 cleanupClient 得加 epoll_fd 参数,这里简单关 fd
        // 实际在调用方会 cleanup
        return;
    }

    auto &st = file_state[fd];
    st.transfer_id = transfer_id;
    st.file_name   = rec->file_name;
    st.saved_name  = rec->saved_name;
    st.file_size   = rec->file_size;
    st.from_uid    = rec->from_uid;
    st.to_uid      = rec->to_uid;
    st.bytes_done  = 0;
    st.eof_sent    = false;

    std::string full_path = FileStore::savedPathFor(*rec);
    st.file_in.open(full_path, std::ios::binary);
    if (!st.file_in)
    {
        cerr << "open file failed: " << full_path << endl;
        file_state.erase(fd);
        return;
    }

    clients[fd].mode = MODE_BINARY_DOWNLOAD;
    cout << "fd=" << fd << " BINARY_DOWNLOAD start, transfer=" << transfer_id << endl;
}