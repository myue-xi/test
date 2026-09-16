#pragma once

#include <ucontext.h>
#include <functional>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cassert>

namespace co {

// 协程状态枚�?
enum class State : uint8_t {
    READY,      // 就绪，可被调�?
    RUNNING,    // 正在运行
    WAITING,    // 等待IO事件
    DONE,       // 执行完毕
};

// 协程�? - 封装ucontext_t
class Coroutine : public std::enable_shared_from_this<Coroutine> {
public:
    using ptr = std::shared_ptr<Coroutine>;
    using Func = std::function<void()>;

    // 创建协程
    static ptr Create(Func func, size_t stack_size = 128 * 1024) {
        return ptr(new Coroutine(std::move(func), stack_size));
    }

    ~Coroutine() {
        if (stack_) {
            free(stack_);
            stack_ = nullptr;
        }
    }

    // 恢复协程执行
    void Resume() {
        assert(state_ != State::DONE);
        State old_state = state_;
        state_ = State::RUNNING;
        swapcontext(&caller_ctx_, &ctx_);
        // 协程让出后回到这�?
        if (state_ == State::RUNNING) {
            state_ = old_state;  // 恢复之前的状�?
        }
    }

    // 让出协程（协程内部调用）
    void Yield() {
        if (state_ == State::RUNNING) {
            state_ = State::READY;
        }
        swapcontext(&ctx_, &caller_ctx_);
    }

    // 等待IO事件
    void Wait() {
        state_ = State::WAITING;
        swapcontext(&ctx_, &caller_ctx_);
    }

    State GetState() const { return state_; }
    void SetState(State s) { state_ = s; }
    uint64_t GetId() const { return id_; }

private:
    Coroutine(Func func, size_t stack_size)
        : func_(std::move(func)), stack_size_(stack_size), state_(State::READY) {
        static uint64_t s_id = 0;
        id_ = ++s_id;
        stack_ = (char*)malloc(stack_size_);
        getcontext(&ctx_);
        ctx_.uc_stack.ss_sp = stack_;
        ctx_.uc_stack.ss_size = stack_size_;
        ctx_.uc_link = nullptr;
        makecontext(&ctx_, &Coroutine::Entry, 1, this);
    }

    static void Entry(Coroutine* co) {
        if (co->func_) {
            co->func_();
        }
        co->state_ = State::DONE;
        // 切换回调用�?
        swapcontext(&co->ctx_, &co->caller_ctx_);
    }

    uint64_t id_;
    Func func_;
    size_t stack_size_;
    char* stack_;
    ucontext_t ctx_;
    ucontext_t caller_ctx_;
    State state_;
};

} // namespace co
