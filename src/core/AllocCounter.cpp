#include "AllocCounter.h"

#include <cstddef>

#if defined(__GLIBC__) && defined(KINETIC_ALLOC_COUNTER)

extern "C" {
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
void* __libc_memalign(size_t, size_t);
void __libc_free(void*);
}

namespace {
// Initial-exec TLS: safe to touch from inside malloc.
__thread uint64_t tlsCount __attribute__((tls_model("initial-exec"))) = 0;
} // namespace

extern "C" {
void* malloc(size_t n)
{
    ++tlsCount;
    return __libc_malloc(n);
}
void* calloc(size_t a, size_t b)
{
    ++tlsCount;
    return __libc_calloc(a, b);
}
void* realloc(void* p, size_t n)
{
    ++tlsCount;
    return __libc_realloc(p, n);
}
void free(void* p) { __libc_free(p); }
void* memalign(size_t al, size_t n)
{
    ++tlsCount;
    return __libc_memalign(al, n);
}
void* aligned_alloc(size_t al, size_t n)
{
    ++tlsCount;
    return __libc_memalign(al, n);
}
int posix_memalign(void** out, size_t al, size_t n)
{
    ++tlsCount;
    void* p = __libc_memalign(al, n);
    if (!p) return 12; // ENOMEM
    *out = p;
    return 0;
}
}

namespace kn::alloc {
uint64_t threadCount() { return tlsCount; }
bool available() { return true; }
} // namespace kn::alloc

#else

namespace kn::alloc {
uint64_t threadCount() { return 0; }
bool available() { return false; }
} // namespace kn::alloc

#endif
