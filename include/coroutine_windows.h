#pragma once

#include <functional>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <windows.h>
#include <winnt.h>

namespace co {

// 协程状态枚举
enum class State : uint8_t {
    READY,      // 就绪，可被调度
    RUNNING,    // 正在运行
    WAITING,    // 等待IO事件
    DONE,       // 执行完毕
};

// 协程类 - 使用Windows Fiber实现
class Coroutine : public std::enable_shared_from_this<Coroutine> {
public:
    using ptr = std::shared_ptr<Coroutine>;
    using Func = std::function<void()>;

    // 创建协程
    static ptr Create(Func func, size_t stack_size = 128 * 1024) {
        return ptr(new Coroutine(std::move(func), stack_size));
    }

    ~Coroutine() {
        if (fiber_) {
            DeleteFiber(fiber_);
            fiber_ = nullptr;
        }
    }

    // 恢复协程执行
    void Resume() {
        assert(state_ != State::DONE);
        State old_state = state_;
        state_ = State::RUNNING;

        // 保存当前fiber，切换到目标fiber
        if (!current_fiber_) {
            current_fiber_ = GetCurrentFiber();
        }
        SwitchToFiber(fiber_);

        // 从协程返回后恢复之前的状态
        if (state_ == State::RUNNING) {
            state_ = old_state;
        }
    }

    // 让出协程（协程内部调用）
    void Yield() {
        if (state_ == State::RUNNING) {
            state_ = State::READY;
        }
        SwitchToFiber(current_fiber_);
    }

    // 等待IO事件
    void Wait() {
        state_ = State::WAITING;
        SwitchToFiber(current_fiber_);
    }

    State GetState() const { return state_; }
    void SetState(State s) { state_ = s; }
    uint64_t GetId() const { return id_; }

private:
    Coroutine(Func func, size_t stack_size)
        : func_(std::move(func)), stack_size_(stack_size), fiber_(nullptr), current_fiber_(nullptr), state_(State::READY) {
        static uint64_t s_id = 0;
        id_ = ++s_id;

        // 创建主fiber
        if (!current_fiber_) {
            current_fiber_ = ConvertThreadToFiber(nullptr);
        }

        // 创建协程fiber
        fiber_ = CreateFiber(stack_size, &Coroutine::Entry, this);
    }

    static void WINAPI Entry(LPVOID param) {
        Coroutine* co = (Coroutine*)param;
        if (co->func_) {
            co->func_();
        }
        co->state_ = State::DONE;
        // 让出执行权
        SwitchToFiber(co->current_fiber_);
    }

    uint64_t id_;
    Func func_;
    size_t stack_size_;
    LPVOID fiber_;
    LPVOID current_fiber_;
    State state_;
};

} // namespace co
