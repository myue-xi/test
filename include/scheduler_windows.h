#pragma once

#include "coroutine_windows.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <mutex>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

namespace co {

// Windows下没有epoll，定义一些兼容的常量
#define EPOLLIN 1
#define EPOLLOUT 2
#define EPOLLERR 4
#define EPOLLHUP 8

// 协程调度器 + IO事件循环
class Scheduler {
public:
    static Scheduler& Instance() {
        static Scheduler inst;
        return inst;
    }

    ~Scheduler() {
        WSACleanup();
    }

    // 创建新协程并加入就绪队列
    Coroutine::ptr CreateCoroutine(Coroutine::Func func) {
        auto co = Coroutine::Create(std::move(func));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_queue_.push(co);
        }
        return co;
    }

    // 将协程重新加入就绪队列
    void EnqueueReady(Coroutine::ptr co) {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_queue_.push(co);
    }

    // 在协程中等待socket上的事件
    // 返回就绪的事件掩码
    uint32_t WaitEvent(SOCKET fd, uint32_t events, int timeout_ms = -1) {
        auto co = GetCurrentCoroutine();
        if (!co) {
            // 不在协程中，直接使用select
            fd_set read_fds, write_fds, error_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            FD_ZERO(&error_fds);

            if (events & EPOLLIN) FD_SET(fd, &read_fds);
            if (events & EPOLLOUT) FD_SET(fd, &write_fds);
            FD_SET(fd, &error_fds);

            timeval timeout;
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;

            if (select(0, &read_fds, &write_fds, &error_fds, 
                      timeout_ms < 0 ? NULL : &timeout) <= 0) {
                return 0;
            }

            uint32_t ret = 0;
            if (FD_ISSET(fd, &read_fds)) ret |= EPOLLIN;
            if (FD_ISSET(fd, &write_fds)) ret |= EPOLLOUT;
            if (FD_ISSET(fd, &error_fds)) ret |= (EPOLLERR | EPOLLHUP);
            return ret;
        }

        // 记录fd -> 协程的映射
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fd_to_co_[fd] = co;
        }

        co->Wait(); // 让出执行权，等待事件

        // 协程被唤醒，从映射中移除
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fd_to_co_.erase(fd);
        }

        return last_events_.count(fd) ? last_events_[fd] : 0;
    }

    // 主事件循环
    void Run() {
        running_ = true;
        while (running_) {
            // 1. 执行就绪队列中的协程
            ProcessReadyQueue();

            // 2. 等待IO事件
            fd_set read_fds, write_fds, error_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            FD_ZERO(&error_fds);

            int max_fd = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& p : fd_to_co_) {
                    SOCKET fd = p.first;
                    FD_SET(fd, &read_fds);
                    FD_SET(fd, &error_fds);
                    if (fd > max_fd) max_fd = fd;
                }
            }

            if (ready_queue_.empty()) {
                // 没有就绪的协程，可以等待一段时间
                timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 10000; // 10ms

                if (select(max_fd + 1, &read_fds, NULL, &error_fds, &timeout) <= 0) {
                    continue;
                }

                // 唤醒等待的协程
                std::lock_guard<std::mutex> lock(mutex_);
                for (int fd = 0; fd <= max_fd; ++fd) {
                    if (FD_ISSET(fd, &read_fds) || FD_ISSET(fd, &error_fds)) {
                        auto it = fd_to_co_.find(fd);
                        if (it != fd_to_co_.end()) {
                            auto co = it->second;
                            uint32_t ev = FD_ISSET(fd, &read_fds) ? EPOLLIN : (EPOLLERR | EPOLLHUP);
                            last_events_[fd] = ev;
                            ready_queue_.push(co);
                            fd_to_co_.erase(it);
                        }
                    }
                }
            }
        }
        running_ = false;
    }

    void Stop() { running_ = false; }

    void SetCurrentCoroutine(Coroutine::ptr co) { current_co_ = co; }
    Coroutine::ptr GetCurrentCoroutine() { return current_co_; }

private:
    Scheduler() : running_(false) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("WSAStartup failed\n");
            exit(1);
        }
    }

    void ProcessReadyQueue() {
        std::queue<Coroutine::ptr> q;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            q.swap(ready_queue_);
        }

        while (!q.empty()) {
            auto co = q.front();
            q.pop();

            if (co->GetState() == State::DONE) continue;

            current_co_ = co;
            co->Resume();
            current_co_ = nullptr;

            // 如果协程让出（READY状态），重新加入队列
            if (co->GetState() == State::READY) {
                std::lock_guard<std::mutex> lock(mutex_);
                ready_queue_.push(co);
            }
        }
    }

    std::atomic<bool> running_;
    std::mutex mutex_;
    std::queue<Coroutine::ptr> ready_queue_;           // 就绪队列
    std::unordered_map<SOCKET, Coroutine::ptr> fd_to_co_; // fd -> 等待的协程
    std::unordered_map<SOCKET, uint32_t> last_events_;    // fd -> 最近的事件
    Coroutine::ptr current_co_;                         // 当前运行的协程
};

} // namespace co
