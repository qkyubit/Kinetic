// Physics-side counterpart of an item. Lives on the physics thread only.
#pragma once

#include "Item.h"
#include "Snapshot.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace kn {

class World;

// Exact dynamic state of a body (float bits as Box2D stores them).
struct BodyState {
    float px = 0, py = 0; // body origin
    float qc = 1, qs = 0; // rotation
    float vx = 0, vy = 0; // linear velocity of the centre of mass
    float w = 0;
    uint32_t awake = 1; // 32-bit so the struct has no padding (compared bytewise)
};
static_assert(sizeof(BodyState) == 32);

// Dynamic state used to rebuild items without losing their motion (keyframes,
// live rebuilds after geometry edits).
// Stored as id-sorted vectors: compact (keyframes keep many of these) and
// cheap to search.
struct RestoreState {
    std::vector<std::pair<ItemId, BodyState>> bodies;
    std::vector<std::pair<ItemId, std::vector<double>>> custom;
    // Force breakdown of sleeping bodies (display only: Box2D does not report
    // contact impulses for bodies restored asleep).
    struct SleepForces {
        ItemId id;
        uint32_t begin, count;
    };
    std::vector<SleepForces> sleepIndex;
    std::vector<ForceEntry> sleepForces;

    void sort()
    {
        std::sort(bodies.begin(), bodies.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::sort(custom.begin(), custom.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    const BodyState* body(ItemId id) const
    {
        const auto it = std::lower_bound(bodies.begin(), bodies.end(), id,
                                         [](const auto& e, ItemId v) { return e.first < v; });
        return (it != bodies.end() && it->first == id) ? &it->second : nullptr;
    }
    const std::vector<double>* customState(ItemId id) const
    {
        const auto it = std::lower_bound(custom.begin(), custom.end(), id,
                                         [](const auto& e, ItemId v) { return e.first < v; });
        return (it != custom.end() && it->first == id) ? &it->second : nullptr;
    }
    size_t byteSize() const
    {
        size_t n = sizeof(*this) + bodies.capacity() * sizeof(bodies[0]);
        for (const auto& e : custom) n += sizeof(e) + e.second.capacity() * sizeof(double);
        n += sleepIndex.capacity() * sizeof(SleepForces) + sleepForces.capacity() * sizeof(ForceEntry);
        return n;
    }
    bool operator==(const RestoreState& o) const;
};

inline bool operator==(const BodyState& a, const BodyState& b)
{
    return std::memcmp(&a, &b, sizeof(BodyState)) == 0;
}

inline bool RestoreState::operator==(const RestoreState& o) const
{
    if (bodies.size() != o.bodies.size() || custom.size() != o.custom.size()) return false;
    for (size_t i = 0; i < bodies.size(); ++i)
        if (bodies[i].first != o.bodies[i].first || !(bodies[i].second == o.bodies[i].second)) return false;
    for (size_t i = 0; i < custom.size(); ++i)
        if (custom[i].first != o.custom[i].first ||
            std::memcmp(custom[i].second.data(), o.custom[i].second.data(),
                        std::min(custom[i].second.size(), o.custom[i].second.size()) * sizeof(double)) != 0 ||
            custom[i].second.size() != o.custom[i].second.size())
            return false;
    return true;
}

struct PotentialEnergy {
    double spring = 0.0;
    double field = 0.0;
};

class ItemRuntime {
public:
    explicit ItemRuntime(ItemId id) : id_(id) {}
    virtual ~ItemRuntime() = default;
    ItemRuntime(const ItemRuntime&) = delete;
    ItemRuntime& operator=(const ItemRuntime&) = delete;

    ItemId id() const { return id_; }

    // Live edit. `changed` has bit i set when property i changed (bit 63 means
    // "63 or higher"). Return false to have the world rebuild this item.
    virtual bool applyEdit(const ItemDef& def, uint64_t changed, World& world);
    // Apply forces before the Box2D step. Must not allocate.
    virtual void preStep(World&) {}
    // Read back constraint forces, update internal state. Must not allocate.
    virtual void postStep(World&) {}
    virtual PotentialEnergy potentialEnergy(const World&) const { return {}; }
    virtual bool snapshot(const World&, ItemSnap&) const { return false; }
    virtual void saveState(std::vector<double>&) const {}
    virtual void loadState(const std::vector<double>&) {}
    // Release Box2D objects. Called exactly once before destruction.
    virtual void destroy(World&) {}
    // Body runtimes return their index in World's body table.
    virtual int bodyIndex() const { return -1; }

private:
    ItemId id_;
};

inline bool ItemRuntime::applyEdit(const ItemDef&, uint64_t, World&) { return false; }

inline uint64_t propBit(int index) { return index >= 63 ? (1ull << 63) : (1ull << index); }

} // namespace kn
