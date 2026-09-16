#include "../include/coroutine_windows.h"
#include "../include/scheduler_windows.h"
#include "../include/net_windows.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

using namespace co;
using namespace co::net;

static const char* LISTEN_IP = "0.0.0.0";
static const uint16_t LISTEN_PORT = 9000;

// 处理客户端连接的协程
void HandleClient(SOCKET client_fd, struct sockaddr_in client_addr) {
    char ip[64];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    uint16_t port = ntohs(client_addr.sin_port);
    printf("[Server] New connection from %s:%d (fd=%d)\n", ip, port, client_fd);

    char buf[4096];
    while (true) {
        ssize_t n = CoRead(client_fd, buf, sizeof(buf));
        if (n <= 0) {
            printf("[Server] Client %s:%d disconnected (fd=%d)\n", ip, port, client_fd);
            break;
        }

        printf("[Server] Received %ld bytes from %s:%d\n", n, ip, port);

        // 将收到的数据回显给客户端
        ssize_t wn = CoWrite(client_fd, buf, n);
        if (wn <= 0) {
            printf("[Server] Write error to %s:%d: %s\n", ip, port, strerror(errno));
            break;
        }
    }

    closesocket(client_fd);
    printf("[Server] Closed connection %s:%d\n", ip, port);
}

// 接受连接的协程
void AcceptLoop(SOCKET listen_fd) {
    printf("[Server] Start accepting connections on fd=%d\n", listen_fd);
    while (true) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_fd = CoAccept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd == INVALID_SOCKET) {
            printf("[Server] Accept error: %s\n", strerror(errno));
            continue;
        }

        // 为每个客户端创建一个协程
        Scheduler::Instance().CreateCoroutine([client_fd, client_addr]() {
            HandleClient(client_fd, client_addr);
        });
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = LISTEN_PORT;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
    }

    // 创建监听socket
    SOCKET listen_fd = TcpListen(LISTEN_IP, port);
    if (listen_fd == INVALID_SOCKET) {
        fprintf(stderr, "[Server] Failed to start server on %s:%d\n", LISTEN_IP, port);
        return 1;
    }

    printf("[Server] Listening on %s:%d (fd=%d)\n", LISTEN_IP, port, listen_fd);

    // 启动协程调度器
    auto& sched = Scheduler::Instance();
    sched.CreateCoroutine([listen_fd]() {
        AcceptLoop(listen_fd);
    });

    // 运行事件循环
    sched.Run();

    closesocket(listen_fd);
    printf("[Server] Server stopped\n");
    return 0;
}
