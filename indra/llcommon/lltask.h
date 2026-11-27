// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   lltask.h
 * @brief  LL::Task<T>, the C++20 coroutine type that replaces LLCoros' fibers (doc/clean/plan.md, "Coroutines").
 *
 * A Task starts at once (initial_suspend is suspend_never) and runs to its first real suspension. When it finishes it
 * hands control straight to the coroutine that awaits it (symmetric transfer): a finishing inner task costs a jump,
 * not a trip through the executor's queue, exactly like a nested function return.
 *
 * A Task owns its frame: destroying a Task that has not finished destroys the suspended frame, and each leaf awaiter
 * unlinks itself from the executor in its destructor, so nothing can resume a dead frame.
 *
 * Two rules:
 * - A Task is awaited by the task that started it. A task runs in the record of the task it was started in (name,
 *   status, local storage), and that record goes when that task ends: a task that another task awaited later would
 *   resume in a record that may be gone. The Task remembers its starter's record and the awaiter checks it.
 * - Parameters of a coroutine returning a Task are taken BY VALUE: a reference would dangle once the caller returns.
 *   A lambda that is itself a coroutine and captures is safe only through LLCoros::launch() and Executor::launch(),
 *   which keep the lambda alive in the task's root frame; anywhere else its captures dangle after the first
 *   suspension. Write a plain function (or a lambda that is not a coroutine) that returns the Task of a by-value
 *   coroutine. clang-tidy's cppcoreguidelines-avoid-reference-coroutine-parameters and
 *   cppcoreguidelines-avoid-capturing-lambda-coroutines enforce this (.github/analysis/clang-tidy.yaml).
 */

#if ! defined(LL_LLTASK_H)
#define LL_LLTASK_H

#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace LL
{

template<typename T = void>
class [[nodiscard]] Task;

namespace detail
{

/// the record of the task that is running now (an LL::CoroData), or null: defined in llexecutor.cpp
const void* currentTaskOwner() noexcept;
/// aborts when @a starter, the record a Task was started in, is not the record that now awaits it
void checkTaskAwaiter(const void* starter);

// What every Task promise shares: the awaiting coroutine and a captured exception.
struct TaskPromiseBase
{
    // the coroutine awaiting this task; empty for a task nobody awaits
    std::coroutine_handle<> mContinuation;
    std::exception_ptr mException;
    // the record of the task this task was started in (null when started outside any task)
    const void* mStarter{currentTaskOwner()};

    std::suspend_never initial_suspend() const noexcept { return {}; }

    struct FinalAwaiter
    {
        bool await_ready() const noexcept { return false; }
        template<typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> self) const noexcept
        {
            // Symmetric transfer: resume the awaiting coroutine without growing the stack.
            std::coroutine_handle<> next = self.promise().mContinuation;
            return next ? next : std::noop_coroutine();
        }
        void await_resume() const noexcept {}
    };
    FinalAwaiter final_suspend() const noexcept { return {}; }

    void unhandled_exception() noexcept { mException = std::current_exception(); }
};

template<typename T>
struct TaskPromise: TaskPromiseBase
{
    std::optional<T> mValue;

    Task<T> get_return_object() noexcept;
    template<typename U = T>
    void return_value(U&& value) { mValue.emplace(std::forward<U>(value)); }
    T take()
    {
        if (mException)
        {
            std::rethrow_exception(mException);
        }
        assert(mValue.has_value());
        return std::move(*mValue);
    }
};

template<>
struct TaskPromise<void>: TaskPromiseBase
{
    Task<void> get_return_object() noexcept;
    void return_void() const noexcept {}
    void take() const
    {
        if (mException)
        {
            std::rethrow_exception(mException);
        }
    }
};

} // namespace detail

template<typename T>
class [[nodiscard]] Task
{
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;
    explicit Task(handle_type handle) noexcept: mHandle(handle) {}
    Task(Task&& other) noexcept: mHandle(std::exchange(other.mHandle, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            mHandle = std::exchange(other.mHandle, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    /// true once the coroutine has run to its end (value or exception stored)
    bool done() const noexcept { return mHandle && mHandle.done(); }
    explicit operator bool() const noexcept { return bool(mHandle); }

    /// The result of a finished task, rethrowing what it threw. For code that is not itself a coroutine
    /// (tests): a coroutine uses co_await.
    T get()
    {
        assert(done());
        return mHandle.promise().take();
    }

    /// co_await task: the awaiting coroutine continues when this one finishes, with its value or its exception. The
    /// awaiter takes the task over, so it lives in the awaiting frame: destroying that frame while it waits destroys
    /// the task too, and the task can never resume a frame that is gone.
    auto operator co_await() && noexcept
    {
        struct Awaiter
        {
            Task mTask;
            bool await_ready() const noexcept
            {
                assert(mTask.mHandle);
                return mTask.mHandle.done();
            }
            void await_suspend(std::coroutine_handle<> awaiting) const
            {
                detail::checkTaskAwaiter(mTask.mHandle.promise().mStarter);
                mTask.mHandle.promise().mContinuation = awaiting;
            }
            T await_resume() const { return mTask.mHandle.promise().take(); }
        };
        return Awaiter{ std::move(*this) };
    }

private:
    void reset() noexcept
    {
        if (mHandle)
        {
            mHandle.destroy();
            mHandle = nullptr;
        }
    }

    handle_type mHandle;
};

/// A callable that starts a task when called: what LLCoros::launch() and Executor::launch() accept.
template<typename F>
concept TaskFactory = std::invocable<F&> && std::same_as<std::invoke_result_t<F&>, Task<void>>;

namespace detail
{
template<typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept
{
    return Task<T>(std::coroutine_handle<TaskPromise<T>>::from_promise(*this));
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept
{
    return Task<void>(std::coroutine_handle<TaskPromise<void>>::from_promise(*this));
}
} // namespace detail

} // namespace LL

#endif /* ! defined(LL_LLTASK_H) */
