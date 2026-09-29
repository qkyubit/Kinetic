#include "TaskPool.h"

#include "AllocCounter.h"

#include <QtGlobal>

#include <algorithm>

namespace kn {

TaskPool::TaskPool(int threads)
{
    for (int i = 0; i < threads; ++i) threads_.emplace_back([this, i] { workerLoop(uint32_t(i + 1)); });
}

TaskPool::~TaskPool()
{
    quit_.store(true);
    signal_.fetch_add(1);
    signal_.notify_all();
    for (std::thread& t : threads_) t.join();
}

void TaskPool::reset()
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    jobCount_ = 0;
}

bool TaskPool::claim(Job* only, Job*& job, int& block)
{
    // Box2D issues a handful of coarse tasks per step, so a lock per claimed
    // block is negligible and keeps slot reuse race-free.
    std::lock_guard<std::mutex> lock(queueMutex_);
    for (int j = 0; j < jobCount_; ++j) {
        Job& candidate = jobs_[size_t(j)];
        if (only && &candidate != only) continue;
        if (candidate.next < candidate.blockCount) {
            job = &candidate;
            block = candidate.next++;
            return true;
        }
    }
    return false;
}

void TaskPool::run(Job& job, int block, uint32_t workerIndex)
{
    const int start = block * job.blockSize;
    const int end = std::min(job.itemCount, start + job.blockSize);
    job.fn(start, end, workerIndex, job.ctx);
    job.done.fetch_add(1, std::memory_order_acq_rel);
}

void TaskPool::workerLoop(uint32_t index)
{
    uint32_t seen = signal_.load(std::memory_order_acquire);
    while (!quit_.load(std::memory_order_acquire)) {
        Job* job = nullptr;
        int block = 0;
        if (claim(nullptr, job, block)) {
            const alloc::Scope allocs;
            run(*job, block, index);
            if (const uint64_t n = allocs.count()) workerAllocs_.fetch_add(n, std::memory_order_relaxed);
            continue;
        }
        // Sleep until new work is published (futex wait, no spinning).
        signal_.wait(seen, std::memory_order_acquire);
        seen = signal_.load(std::memory_order_acquire);
    }
}

void* TaskPool::enqueue(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext)
{
    auto* pool = static_cast<TaskPool*>(userContext);
    Job* job = nullptr;
    {
        std::lock_guard<std::mutex> lock(pool->queueMutex_);
        if (itemCount > 0 && pool->jobCount_ < kMaxJobs && !pool->threads_.empty()) {
            job = &pool->jobs_[size_t(pool->jobCount_++)];
            job->fn = task;
            job->ctx = taskContext;
            job->itemCount = itemCount;
            const int workers = pool->workerCount();
            const int blocks = std::max(1, std::min(workers, itemCount / std::max(minRange, 1)));
            job->blockSize = (itemCount + blocks - 1) / blocks;
            job->blockCount = (itemCount + job->blockSize - 1) / job->blockSize;
            job->next = 0;
            job->done.store(0, std::memory_order_relaxed);
        }
    }
    if (!job) {
        task(0, itemCount, 0, taskContext); // no slot or no workers: run inline
        return nullptr;
    }
    pool->signal_.fetch_add(1, std::memory_order_acq_rel);
    pool->signal_.notify_all();
    return job;
}

void TaskPool::finish(void* userTask, void* userContext)
{
    auto* pool = static_cast<TaskPool*>(userContext);
    auto* job = static_cast<Job*>(userTask);
    // Help with our own task (as worker 0), then wait for blocks other threads run.
    Job* claimed = nullptr;
    int block = 0;
    while (pool->claim(job, claimed, block)) pool->run(*claimed, block, 0);
    while (job->done.load(std::memory_order_acquire) < job->blockCount) std::this_thread::yield();
}

namespace {
int gConfiguredWorkers = 0;
}

int TaskPool::automaticWorkers()
{
    // Box2D scales best on a few fast cores; beyond ~4 workers the solver's
    // lockstep synchronisation costs more than it gains on laptop CPUs.
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    return int(std::clamp<unsigned>(hw / 2, 1u, 4u));
}

void TaskPool::configure(int workers) { gConfiguredWorkers = workers; }

TaskPool* TaskPool::instance()
{
    static TaskPool* pool = [] {
        int workers = gConfiguredWorkers;
        if (workers <= 0) workers = qEnvironmentVariableIntValue("KINETIC_THREADS");
        if (workers <= 0) workers = automaticWorkers();
        return new TaskPool(std::clamp(workers, 1, 32) - 1); // lives for the whole process
    }();
    return pool;
}

} // namespace kn
