// Minimal task system for Box2D v3's multithreaded step.
//
// Box2D hands us parallel-for tasks through b2EnqueueTaskCallback. Worker
// threads claim blocks of items; the thread that calls finishTask (the physics
// thread, worker index 0) helps with the blocks of the task it waits for. This
// matters for Box2D's solver, whose per-worker tasks spin in lockstep. Idle
// workers sleep on an atomic wait, so a paused simulation costs no CPU.
#pragma once

#include <box2d/types.h>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace kn {

class TaskPool {
public:
    // `threads` worker threads plus the calling thread.
    explicit TaskPool(int threads);
    ~TaskPool();
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    int workerCount() const { return int(threads_.size()) + 1; }
    // Heap allocations made by worker threads while running tasks.
    uint64_t workerAllocations() const { return workerAllocs_.load(std::memory_order_relaxed); }
    // Serialises world steps that share the pool; call reset() under it.
    std::mutex& stepMutex() { return stepMutex_; }
    // Recycles task slots. Only call when no task is outstanding.
    void reset();

    static void* enqueue(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext);
    static void finish(void* userTask, void* userContext);

    // Process-wide pool, created on first use. The worker count is a machine
    // setting (results do not depend on it): configure() before first use, or
    // set KINETIC_THREADS; 0 = automatic.
    static TaskPool* instance();
    static void configure(int workers);
    static int automaticWorkers();

private:
    struct Job {
        b2TaskCallback* fn = nullptr;
        void* ctx = nullptr;
        int itemCount = 0;
        int blockSize = 1;
        int blockCount = 0;
        int next = 0; // guarded by queueMutex_
        std::atomic<int> done{0};
    };
    static constexpr int kMaxJobs = 256;

    void workerLoop(uint32_t index);
    // Claims the next block of `only` (or of any job when null).
    bool claim(Job* only, Job*& job, int& block);
    void run(Job& job, int block, uint32_t workerIndex);

    std::array<Job, kMaxJobs> jobs_;
    int jobCount_ = 0; // guarded by queueMutex_
    std::mutex queueMutex_;
    std::atomic<uint32_t> signal_{0};
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> workerAllocs_{0};
    std::vector<std::thread> threads_;
    std::mutex stepMutex_;
};

} // namespace kn
