// Immutable simulation snapshot published by the physics thread through a triple
// buffer. Plain data only; the vectors keep their capacity between publishes so
// steady-state publishing does not allocate.
#pragma once

#include "Forces.h"
#include "Property.h"

#include <vector>

namespace kn {

constexpr int kMaxForceEntries = 12;

struct ForceEntry {
    float fx = 0, fy = 0;
    float torque = 0; // about the centre of mass
    uint8_t source = 0;
};

struct BodySnap {
    ItemId id = 0;
    uint8_t awake = 0;
    uint8_t bodyType = 2; // 0 static, 1 kinematic, 2 dynamic
    uint8_t forceCount = 0;
    float x = 0, y = 0, c = 1, s = 0; // body origin pose
    float cx = 0, cy = 0;             // world centre of mass
    float vx = 0, vy = 0, w = 0;      // velocity of the centre of mass, angular velocity
    float ax = 0, ay = 0, alpha = 0;  // accelerations over the last step
    float mass = 0, inertia = 0;      // inertia about the centre of mass
    float fx = 0, fy = 0, torque = 0; // net tagged force
    ForceEntry forces[kMaxForceEntries];

    Pose pose() const { return Pose{{x, y}, c, s}; }
};

// Generic per-item live values for non-body items (connector end points and
// forces, piston extension, equation state...). Meaning of v[] is item specific.
struct ItemSnap {
    ItemId id = 0;
    float v[8] = {};
};

struct EnergySnap {
    double kinetic = 0;   // translational + rotational
    double gravity = 0;   // uniform global gravity potential
    double spring = 0;    // elastic potential of springs
    double field = 0;     // point-mass gravity zones and electric potential
    double total = 0;
    double baseline = 0;  // total at simulation start / last edit
    double driftPercent() const
    {
        const double ref = std::abs(baseline) > 1e-9 ? std::abs(baseline) : 1.0;
        return 100.0 * (total - baseline) / ref;
    }
};

struct Snapshot {
    bool active = false;  // a simulation exists (false: show the document)
    uint64_t generation = 0; // SimStart::generation of the simulation that produced this
    bool running = false; // advancing in real time
    uint64_t step = 0;
    double time = 0;
    double dt = 1.0 / 120.0;
    uint64_t structureVersion = 0; // changes when bodies/items are added or removed
    uint64_t headStep = 0;          // furthest recorded step
    bool recording = false;
    double speed = 1.0;
    float stepMs = 0;          // average wall time per physics step
    std::vector<BodySnap> bodies;
    std::vector<ItemSnap> items;
    EnergySnap energy;
};

struct TriggerEvent {
    double time = 0;
    uint64_t step = 0;
    ItemId trigger = 0;
    ItemId body = 0;
    bool enter = true;
};

} // namespace kn
