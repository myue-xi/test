#include "../include/coroutine_windows.h"
#include "../include/scheduler_windows.h"
#include "../include/net_windows.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <windows.h>

using namespace co;
using namespace co::net;

static const char* SERVER_IP = "127.0.0.1";
static const uint16_t SERVER_PORT = 9000;

// 处理连接的协程
void HandleConnection(SOCKET client_fd) {
    printf("[Client] Connected to server (fd=%d)\n", client_fd);

    // 发送测试消息
    std::string msg = "Hello from client!";
    ssize_t wn = CoWrite(client_fd, msg.c_str(), msg.length());
    if (wn <= 0) {
        printf("[Client] Write error: %s\n", strerror(errno));
        closesocket(client_fd);
        return;
    }
    printf("[Client] Sent %ld bytes: %s\n", wn, msg.c_str());

    // 读取回显
    char buf[4096];
    ssize_t rn = CoRead(client_fd, buf, sizeof(buf));
    if (rn <= 0) {
        printf("[Client] Read error: %s\n", strerror(errno));
    } else {
        printf("[Client] Received %ld bytes: %.*s\n", rn, (int)rn, buf);
    }

    closesocket(client_fd);
    printf("[Client] Connection closed\n");
}

// 测试协程
void TestCoroutine() {
    printf("[Client] Creating coroutine...\n");
    Scheduler::Instance().CreateCoroutine([]() {
        printf("[Client] Coroutine started\n");
        Sleep(100); // Windows下的sleep
        printf("[Client] Coroutine finished\n");
    });
}

int main(int argc, char* argv[]) {
    uint16_t port = SERVER_PORT;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
    }

    // 创建协程调度器
    auto& sched = Scheduler::Instance();

    // 测试普通协程
    TestCoroutine();

    // 创建TCP连接
    SOCKET client_fd = CoConnect(SERVER_IP, port);
    if (client_fd == INVALID_SOCKET) {
        fprintf(stderr, "[Client] Failed to connect to %s:%d\n", SERVER_IP, port);
        return 1;
    }

    // 创建连接处理协程
    sched.CreateCoroutine([client_fd]() {
        HandleConnection(client_fd);
    });

    // 运行事件循环
    sched.Run();

    return 0;
}
