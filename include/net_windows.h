#pragma once

#include "coroutine_windows.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace co {
namespace net {

// 设置文件描述符为非阻塞
inline bool SetNonBlocking(SOCKET fd) {
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
}

// 协程化读取 - 等待可读事件后读取
inline ssize_t CoRead(SOCKET fd, void* buf, size_t len, int timeout_ms = -1) {
    while (true) {
        ssize_t n = recv(fd, (char*)buf, (int)len, 0);
        if (n > 0) return n;
        if (n == 0) return 0;
        if (WSAGetLastError() == WSAEINTR) continue;
        if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS) {
            // 等待可读事件
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(fd, &read_fds);

            timeval timeout;
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;

            if (select(0, &read_fds, NULL, NULL, timeout_ms < 0 ? NULL : &timeout) > 0) {
                continue;
            }
            return -1; // 超时
        }
        return -1; // 其他错误
    }
}

// 协程化写入 - 等待可写事件后写入
inline ssize_t CoWrite(SOCKET fd, const void* buf, size_t len, int timeout_ms = -1) {
    size_t written = 0;
    const char* p = (const char*)buf;
    while (written < len) {
        ssize_t n = send(fd, p + written, (int)(len - written), 0);
        if (n > 0) {
            written += n;
            continue;
        }
        if (n == 0) return written;
        if (WSAGetLastError() == WSAEINTR) continue;
        if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS) {
            // 等待可写事件
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(fd, &write_fds);

            timeval timeout;
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;

            if (select(0, NULL, &write_fds, NULL, timeout_ms < 0 ? NULL : &timeout) > 0) {
                continue;
            }
            return -1; // 超时
        }
        return -1; // 其他错误
    }
    return written;
}

// 协程化接受连接
inline SOCKET CoAccept(SOCKET listen_fd, struct sockaddr* addr = nullptr, int* addrlen = nullptr) {
    while (true) {
        SOCKET fd = accept(listen_fd, addr, (socklen_t*)addrlen);
        if (fd != INVALID_SOCKET) return fd;
        if (WSAGetLastError() == WSAEINTR) continue;
        if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS) {
            // 等待可读事件
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(listen_fd, &read_fds);

            if (select(0, &read_fds, NULL, NULL, NULL) > 0) {
                continue;
            }
        }
        return INVALID_SOCKET;
    }
}

// 创建TCP监听socket
inline SOCKET TcpListen(const char* ip, uint16_t port, int backlog = 128) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return INVALID_SOCKET;
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        printf("socket failed\n");
        WSACleanup();
        return INVALID_SOCKET;
    }

    BOOL opt = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("inet_pton failed\n");
        closesocket(fd);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind failed\n");
        closesocket(fd);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (listen(fd, backlog) == SOCKET_ERROR) {
        printf("listen failed\n");
        closesocket(fd);
        WSACleanup();
        return INVALID_SOCKET;
    }

    return fd;
}

// 创建TCP连接socket（协程化连接）
inline SOCKET CoConnect(const char* ip, uint16_t port, int timeout_ms = 5000) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return INVALID_SOCKET;
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        printf("socket failed\n");
        WSACleanup();
        return INVALID_SOCKET;
    }

    SetNonBlocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("inet_pton failed\n");
        closesocket(fd);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINPROGRESS) {
            printf("connect failed\n");
            closesocket(fd);
            WSACleanup();
            return INVALID_SOCKET;
        }
    }

    // 等待连接完成
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(0, NULL, &write_fds, NULL, &timeout) > 0) {
        // 检查连接是否成功
        int error = 0;
        int len = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0 && error == 0) {
            return fd;
        }
    }

    closesocket(fd);
    WSACleanup();
    return INVALID_SOCKET;
}

// 读取一行（以\n结尾）
inline std::string CoReadLine(SOCKET fd, int timeout_ms = -1) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = CoRead(fd, &c, 1, timeout_ms);
        if (n <= 0) break;
        line.push_back(c);
        if (c == '\n') break;
    }
    return line;
}

// 协程化读取指定长度
inline ssize_t CoReadN(SOCKET fd, void* buf, size_t len, int timeout_ms = -1) {
    size_t readn = 0;
    char* p = (char*)buf;
    while (readn < len) {
        ssize_t n = CoRead(fd, p + readn, len - readn, timeout_ms);
        if (n <= 0) return n == 0 ? readn : -1;
        readn += n;
    }
    return readn;
}

} // namespace net
} // namespace co
