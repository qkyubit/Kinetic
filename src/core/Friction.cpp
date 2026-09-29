#include "Friction.h"

#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

namespace kn::friction {

namespace {
struct Pair {
    float muStatic = 0, muKinetic = 0;
};
// Fixed capacity: entries are immutable once published, so the callback (on
// Box2D worker threads) reads them without locks and the table never moves.
constexpr int kCapacity = 1 << 14;
std::array<Pair, kCapacity> gPairs;
std::atomic<int> gCount{0};
std::mutex gMutex;
} // namespace

int materialId(float muStatic, float muKinetic)
{
    std::lock_guard<std::mutex> lock(gMutex);
    const int n = gCount.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        if (gPairs[size_t(i)].muStatic == muStatic && gPairs[size_t(i)].muKinetic == muKinetic) return i + 1;
    if (n >= kCapacity) return 0; // table full: plain Coulomb mixing for this shape
    gPairs[size_t(n)] = {muStatic, muKinetic};
    gCount.store(n + 1, std::memory_order_release);
    return n + 1;
}

float mix(float frictionA, int materialA, float frictionB, int materialB)
{
    const int n = gCount.load(std::memory_order_acquire);
    const Pair* a = materialA > 0 && materialA <= n ? &gPairs[size_t(materialA - 1)] : nullptr;
    const Pair* b = materialB > 0 && materialB <= n ? &gPairs[size_t(materialB - 1)] : nullptr;
    // A shape is "sliding" when its current coefficient is its kinetic one.
    const bool slidingA = a && a->muKinetic != a->muStatic && frictionA == a->muKinetic;
    const bool slidingB = b && b->muKinetic != b->muStatic && frictionB == b->muKinetic;
    const bool kinetic = slidingA || slidingB;
    const float fa = a ? (kinetic ? a->muKinetic : a->muStatic) : frictionA;
    const float fb = b ? (kinetic ? b->muKinetic : b->muStatic) : frictionB;
    return std::sqrt(fa * fb);
}

} // namespace kn::friction
