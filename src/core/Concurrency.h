// Lock-free primitives connecting the UI and physics threads.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace kn {

// Single-producer single-consumer ring buffer. push() is called by exactly one
// thread and pop() by exactly one other thread.
template <class T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "capacity must be a power of two");

public:
    bool push(T&& v)
    {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) == N) return false;
        buf_[h & (N - 1)] = std::move(v);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out)
    {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        out = std::move(buf_[t & (N - 1)]);
        buf_[t & (N - 1)] = T{};
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    bool empty() const { return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire); }

private:
    std::array<T, N> buf_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// Triple buffer: the writer always has a private back buffer, the reader a
// private front buffer; publish/acquire swap through the shared middle slot.
// Neither side ever blocks the other.
template <class T>
class TripleBuffer {
public:
    T& back() { return bufs_[back_]; }
    void publish()
    {
        // Swap back with middle and mark it fresh.
        const uint8_t old = middle_.exchange(uint8_t(back_ | kFresh), std::memory_order_acq_rel);
        back_ = old & kIndexMask;
    }
    // Returns true if a newer buffer became the front buffer.
    bool acquire()
    {
        if (!(middle_.load(std::memory_order_acquire) & kFresh)) return false;
        const uint8_t old = middle_.exchange(uint8_t(front_), std::memory_order_acq_rel);
        front_ = old & kIndexMask;
        return true;
    }
    const T& front() const { return bufs_[front_]; }

private:
    static constexpr uint8_t kFresh = 0x4;
    static constexpr uint8_t kIndexMask = 0x3;
    T bufs_[3];
    uint8_t back_ = 0;
    uint8_t front_ = 1;
    std::atomic<uint8_t> middle_{2};
};

} // namespace kn
