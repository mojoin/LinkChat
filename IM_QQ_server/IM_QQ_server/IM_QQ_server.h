// IM_QQ_server.h: 跨平台套接字头文件
#pragma once

// ============== 平台适配 ==============
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    typedef int socket_t;
    #define INVALID_SOCKET  (-1)
    #define SOCKET_ERROR    (-1)
    #define closesocket(s)  close(s)
#endif

#include <iostream>
#include <cstring>

// ============== 错误打印 ==============
void print_error(const char* message);
#define LOG_ERROR(message) print_error(message)


// TODO: 在此处引用程序需要的其他标头。