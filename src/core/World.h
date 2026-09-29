// The physics world: a Box2D v3 world plus the item runtimes that contribute
// forces, constraints and fields. Owned and used by the physics thread only.
#pragma once

#include "Runtime.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace kn {

struct WorldSettings {
    Vec2 gravity{0.0, -9.81};
    int hz = 120;
    int substeps = 4;
    bool sleep = true;
    bool continuous = true;
    double airDensity = 1.225;
    bool airDrag = false; // quadratic air drag on every body
    bool mutualGravity = false; // Newtonian attraction between all dynamic bodies
    double gravitationalConstant = 6.674e-11;
    double jointHertz = 60.0;
    double jointDamping = 2.0;
    double coulombSoftening = 0.05;
    double restitutionThreshold = 1.0;
    double contactHertz = 30.0;
    double contactDamping = 10.0;
    int keyframeInterval = 240;

    static WorldSettings fromDef(const ItemDef& worldDef);
};

struct BodyRec {
    ItemId item = 0;
    b2BodyId id = b2_nullBodyId;
    bool alive = false;
    uint8_t type = 2; // b2BodyType
    float mass = 0, invMass = 0, inertia = 0; // inertia about the centre of mass
    float gravityScale = 1;
    float linearDamping = 0, angularDamping = 0;
    float charge = 0;           // coulombs
    b2Vec2 magMoment{0, 0};     // local-frame magnetic dipole moment, A·m²
    float dragCoefficient = 1.0f;
    float muStatic = 0.6f, muKinetic = 0.6f;
    bool sliding = false;
    bool slidingNext = false;
    bool refreshForces = true;
    float tangentSpeed = 0.0f; // conveyor surface speed of this body's shapes
    std::vector<Shape> shapes;     // local geometry, used by fluids/wind
    std::vector<b2ShapeId> shapeIds;
    // State refreshed every step.
    b2Vec2 origin{0, 0};
    b2Rot rot{1, 0};
    b2Vec2 com{0, 0};
    b2Vec2 v{0, 0}, prevV{0, 0};
    float w = 0, prevW = 0;
    b2Vec2 a{0, 0};
    float alpha = 0;
    bool awake = true;
    ForceEntry forces[kMaxForceEntries];
    int forceCount = 0;
    // Breakdown from the last awake step; shown while the body sleeps.
    ForceEntry lastForces[kMaxForceEntries];
    int lastForceCount = 0;

    Pose pose() const { return Pose{{origin.x, origin.y}, rot.c, rot.s}; }
};

class World {
public:
    explicit World(const ItemDef& worldDef);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // --- structure (not real-time; may allocate) ------------------------------
    // Builds all items in order: bodies first, then everything else.
    void build(const std::vector<ItemDefPtr>& defs, const RestoreState* restore);
    void addItem(ItemDefPtr def, const RestoreState* restore = nullptr);
    void removeItem(ItemId id);
    // Applies an edited definition. Rebuilds the runtime (and its dependents)
    // when it cannot apply the change live.
    void updateItem(ItemDefPtr def, uint64_t changed);
    void setWorldDef(const ItemDef& worldDef);
    // Teleports a body (origin pose) and sets its velocity; wakes it.
    void setBodyState(ItemId item, const Pose& pose, Vec2 v, double w);
    void capture(RestoreState& out) const;
    const std::vector<ItemDefPtr>& defs() const { return defs_; }
    const ItemDef* def(ItemId id) const;
    ItemRuntime* runtime(ItemId id) const;

    // --- stepping (real-time; must not allocate) ------------------------------------
    void step();
    void writeSnapshot(Snapshot& s) const;
    EnergySnap energy() const;
    void resetEnergyBaseline() { baselineValid_ = false; }
    double energyBaseline() const { return baseline_; }
    bool energyBaselineValid() const { return baselineValid_; }
    void setEnergyBaseline(double v, bool valid)
    {
        baseline_ = v;
        baselineValid_ = valid;
    }

    // --- API for runtimes -------------------------------------------------------------
    b2WorldId id() const { return id_; }
    b2BodyId groundBody() const { return ground_; }
    const WorldSettings& settings() const { return settings_; }
    float dt() const { return 1.0f / float(settings_.hz); }
    // External forces are held constant over Box2D's n sub-steps, which moves
    // positions by (n+1)/(2n)·dt²·F/m instead of symplectic Euler's dt²·F/m and
    // makes explicit position-dependent forces gain energy. Evaluating such
    // forces at the predicted position x + alpha·dt·v with alpha = (n-1)/(2n)
    // makes the step exactly area-preserving for linear forces (springs) and
    // restores bounded energy error for smooth fields.
    float predictAlpha() const { return float(settings_.substeps - 1) / float(2 * settings_.substeps); }
    // Position of a body-fixed world point, extrapolated as described above.
    b2Vec2 predictPoint(int bodyIndex, b2Vec2 p) const
    {
        const BodyRec& b = bodies_[size_t(bodyIndex)];
        if (b.type == b2_staticBody) return p;
        const b2Vec2 v = b2Add(b.v, b2CrossSV(b.w, b2Sub(p, b.com)));
        return b2MulAdd(p, predictAlpha() * dt(), v);
    }
    double time() const { return time_; }
    uint64_t stepIndex() const { return step_; }
    void setClock(uint64_t step, double time) { step_ = step; time_ = time; }
    uint64_t structureVersion() const { return structureVersion_; }
    // Heap allocations during the last step (all threads), and how many of
    // them Box2D made from its own allocator.
    uint64_t lastStepAllocations() const { return stepAllocs_; }
    uint64_t lastStepBox2dAllocations() const { return stepBox2dAllocs_; }

    int registerBody(ItemId item, b2BodyId id);
    void unregisterBody(int index);
    int bodyIndex(ItemId item) const;
    BodyRec& body(int index) { return bodies_[size_t(index)]; }
    const BodyRec& body(int index) const { return bodies_[size_t(index)]; }
    int bodyCapacity() const { return int(bodies_.size()); }
    // Refreshes cached mass properties after shapes/density change.
    void refreshMass(int index);
    // b2BodyId for an item id (ground body for 0 / unknown).
    b2BodyId bodyIdOr(ItemId item) const;

    // Applies a force at a world point and records it under `source`.
    void addForce(int index, ForceSource source, b2Vec2 force, b2Vec2 point, bool wake = true);
    void addForceAtCenter(int index, ForceSource source, b2Vec2 force, bool wake = true);
    void addTorque(int index, ForceSource source, float torque, bool wake = true);
    // Field forces act on awake dynamic bodies only: a body resting in a field
    // may fall asleep like one resting under gravity. Field items call
    // wakeAll() when they appear or change.
    void addFieldForce(int index, ForceSource source, b2Vec2 force);
    void addFieldForce(int index, ForceSource source, b2Vec2 force, b2Vec2 point);
    void addFieldTorque(int index, ForceSource source, float torque);
    void wakeAll();
    // Width of the body's silhouette across `axis` (unit vector), per metre of depth.
    double silhouetteWidth(int index, b2Vec2 axis) const;
    // Records a force that Box2D applies itself (contacts, joints, gravity).
    void record(int index, ForceSource source, b2Vec2 force, float torque);

    // Events (trigger enter/exit) produced during steps, drained by the owner.
    void emitEvent(const TriggerEvent& e)
    {
        if (events_.size() < events_.capacity()) events_.push_back(e); // never reallocates in a step
        else ++droppedEvents_;
    }
    const std::vector<TriggerEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }

    // Named constants (Constant items) for expressions. Slots are created by
    // name at structure time and hold NaN until a Constant item sets them.
    static constexpr int kMaxConstants = 256;
    int constantSlot(const QString& name); // -1 when the table is full
    const double* constantValues() const { return constantValues_.data(); }
    void setConstantValue(int slot, double v) { constantValues_[size_t(slot)] = v; }

private:
    struct Entry {
        ItemDefPtr def;
        std::unique_ptr<ItemRuntime> rt;
    };

    void createRuntime(Entry& e, const RestoreState* restore);
    void destroyRuntime(Entry& e);
    bool referencesSatisfied(const ItemDef& d) const;
    void createDormant(const RestoreState* restore);
    std::vector<size_t> dependentsOf(ItemId id) const;
    void reindex();
    void computeContactForces();
    void dispatchSensorEvents();
    void contactsForBody(int index, std::vector<b2ContactData>& scratch);
    int indexOfBody(b2BodyId id) const;
    b2Vec2 pointVelocity(int index, b2Vec2 p) const;
    void updateFrictionState(BodyRec& b);
    void refreshBodyState(BodyRec& b);
    void applySettings();
    void applyWorldFields();

    WorldSettings settings_;
    b2WorldId id_ = b2_nullWorldId;
    b2BodyId ground_ = b2_nullBodyId;
    std::vector<Entry> items_;
    std::vector<ItemDefPtr> defs_;
    std::unordered_map<ItemId, size_t> itemIndex_;
    std::vector<BodyRec> bodies_;
    std::vector<int> freeBodies_;
    std::vector<int> charged_, magnets_, massive_; // rebuilt every step; capacity reserved at registration
    std::unordered_map<ItemId, int> bodyIndex_;
    std::vector<std::vector<b2ContactData>> contactScratch_; // one per worker
    std::vector<QString> constantNames_;
    std::vector<double> constantValues_ = std::vector<double>(kMaxConstants, std::nan(""));
    std::vector<TriggerEvent> events_;
    uint64_t droppedEvents_ = 0;
    uint64_t step_ = 0;
    double time_ = 0.0;
    uint64_t structureVersion_ = 1;
    bool statesDirty_ = true;
    uint64_t stepAllocs_ = 0, stepBox2dAllocs_ = 0;
    mutable bool baselineValid_ = false;
    mutable double baseline_ = 0.0;
};

} // namespace kn
