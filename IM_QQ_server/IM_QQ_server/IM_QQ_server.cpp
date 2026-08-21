#include "IM_QQ_server.h"
#include "net.h"
#include "user_store.h"
#include "friend_store.h"
#include "friend_request_store.h"

using namespace std;

// ===================== 错误打印工具 =====================
void print_error(const char *message)
{
#ifdef _WIN32
    int code = WSAGetLastError();
#else
    int code = errno;
#endif
    cerr << "[ERROR] " << __FILE__ << ":" << __LINE__
         << " in " << __func__ << "(): " << message
         << " (code=" << code;
#ifndef _WIN32
    cerr << ": " << strerror(code);   // Windows 没有 strerror(errno)
#endif
    cerr << ")\n";
}

// ===================== 简易客户端测试模式 =====================
void client()
{
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        cerr << "socket() failed: " << errno << endl;
        return;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9527);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        cerr << "connect() failed: " << errno << endl;
        return;
    }
    cout << "Connected to server" << endl;
    char buf[1024];
    while (true)
    {
        cout << "> ";
        cin.getline(buf, sizeof(buf));
        if (strlen(buf) == 0)
            continue;
        send(sock, buf, strlen(buf), 0);
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            break;
        }
        cout << "Received: " << buf << endl;
    }
    cout << "Disconnected from server" << endl;
    closesocket(sock);
}

// ===================== 主入口 =====================
int main(int argc, char *argv[])
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    if (argc < 2)
    {
        cerr << "Usage: echo server|client (1|2)\n";
        return 1;
    }
    if (!strcmp(argv[1], "server") || !strcmp(argv[1], "1"))
    {
        // 启动前先把用户表从 data/user.json 加载进内存
        if (!UserStore::instance().loadFromFile("data/user.json")) {
            cerr << "Failed to load users from data/user.json; login will reject all.\n";
        }
        // 启动前把好友关系从 data/friends.json 加载进内存
        if (!FriendStore::instance().loadFromFile("data/friends.json")) {
            cerr << "Failed to load friends from data/friends.json; get_friends will return empty.\n";
        }
        // 启动前把好友申请从 data/friend_requests.json 加载进内存（首次运行文件不存在也算成功）
        if (!FriendRequestStore::instance().loadFromFile("data/friend_requests.json")) {
            cerr << "Failed to load friend_requests from data/friend_requests.json; add_friend_request may misbehave.\n";
        }
        startServer(9527);    // ← 网络层入口（业务细节全部在 net.cpp 里）
    }
    else if (!strcmp(argv[1], "client") || !strcmp(argv[1], "2"))
    {
        client();
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}