// Per-thread heap allocation counter. On glibc the executable interposes
// malloc/calloc/realloc/aligned allocators (see AllocCounter.cpp), so every
// allocation in the process is counted, including Qt's and Box2D's.
#pragma once

#include <cstdint>

namespace kn::alloc {

// Number of allocations made by the calling thread since it started.
uint64_t threadCount();
// True when the interposer is linked in and active.
bool available();

class Scope {
public:
    Scope() : start_(threadCount()) {}
    uint64_t count() const { return threadCount() - start_; }

private:
    uint64_t start_;
};

} // namespace kn::alloc
