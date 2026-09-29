// Heap allocation counters. On glibc the executable interposes malloc/calloc/
// realloc/aligned allocators (see AllocCounter.cpp), so every allocation in the
// process is counted per thread, including Qt's and Box2D's. Box2D allocations
// are additionally counted through b2SetAllocator, so a physics step can report
// how many came from Box2D's own pools versus Kinetic code.
#pragma once

#include <cstdint>

namespace kn::alloc {

// Number of allocations made by the calling thread since it started.
uint64_t threadCount();
// Allocations made through Box2D's allocator (all threads).
uint64_t box2dCount();
// Routes Box2D's allocations through the counting hook. Idempotent.
void installBox2dCounter();
// True when the malloc interposer is linked in and active.
bool available();

class Scope {
public:
    Scope() : start_(threadCount()) {}
    uint64_t count() const { return threadCount() - start_; }

private:
    uint64_t start_;
};

} // namespace kn::alloc
