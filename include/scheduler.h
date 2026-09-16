#pragma once

#include "coroutine.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <atomic>

namespace co {

// 协程调度器 + Epoll事件循环
class Scheduler {
public:
    static Scheduler& Instance() {
        static Scheduler inst;
        return inst;
    }

    ~Scheduler() {
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
        }
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

    // 在协程中等待文件描述符上的事件
    // 返回就绪的事件掩码
    uint32_t WaitEvent(int fd, uint32_t events, int timeout_ms = -1) {
        auto co = GetCurrentCoroutine();
        if (!co) {
            // 不在协程中，直接使用epoll_wait
            struct epoll_event ev;
            ev.events = events;
            ev.data.fd = fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);

            struct epoll_event events_arr[1];
            int n = epoll_wait(epoll_fd_, events_arr, 1, timeout_ms);
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            return n > 0 ? events_arr[0].events : 0;
        }

        // 注册epoll事件
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl ADD");
            return 0;
        }

        // 记录fd -> 协程的映射
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fd_to_co_[fd] = co;
        }

        co->Wait(); // 让出执行权，等待事件

        // 协程被唤醒，从epoll中移除
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
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

            // 2. 等待epoll事件
            struct epoll_event events[128];
            int n = epoll_wait(epoll_fd_, events, 128, ready_queue_.empty() ? 10 : 0);

            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                break;
            }

            // 3. 唤醒等待的协程
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;

                std::lock_guard<std::mutex> lock(mutex_);
                auto it = fd_to_co_.find(fd);
                if (it != fd_to_co_.end()) {
                    auto co = it->second;
                    last_events_[fd] = ev;
                    ready_queue_.push(co);
                    fd_to_co_.erase(it);
                }
            }

            // 4. 检查是否还有活跃协程
            if (n == 0 && ready_queue_.empty() && fd_to_co_.empty()) {
                // 没有更多事件和协程，可以退出
                // 但我们继续运行直到外部调用Stop()
            }
        }
        running_ = false;
    }

    void Stop() { running_ = false; }

    int GetEpollFd() const { return epoll_fd_; }

    void SetCurrentCoroutine(Coroutine::ptr co) { current_co_ = co; }
    Coroutine::ptr GetCurrentCoroutine() { return current_co_; }

private:
    Scheduler() : running_(false) {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0) {
            perror("epoll_create1");
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

    int epoll_fd_;
    std::atomic<bool> running_;
    std::mutex mutex_;
    std::queue<Coroutine::ptr> ready_queue_;           // 就绪队列
    std::unordered_map<int, Coroutine::ptr> fd_to_co_; // fd -> 等待的协程
    std::unordered_map<int, uint32_t> last_events_;    // fd -> 最近的事件
    Coroutine::ptr current_co_;                         // 当前运行的协程
};

} // namespace co
