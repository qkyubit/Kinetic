#include "World.h"

#include <algorithm>
#include <atomic>

namespace kn {

namespace {
// Process-wide so versions never repeat across rebuilt worlds.
std::atomic<uint64_t> gStructureVersion{1};
uint64_t nextStructureVersion() { return ++gStructureVersion; }

double numberProp(const ItemDef& d, const char* key, double fallback)
{
    const int i = d.type ? d.type->schema().indexOf(QLatin1String(key)) : -1;
    if (i < 0) return fallback;
    return numericValue(d.props[size_t(i)]);
}
} // namespace

WorldSettings WorldSettings::fromDef(const ItemDef& d)
{
    WorldSettings s;
    if (!d.type) return s;
    const int gi = d.type->schema().indexOf(QStringLiteral("gravity"));
    if (gi >= 0) s.gravity = d.v(gi);
    s.hz = int(numberProp(d, "physicsHz", 120));
    s.substeps = int(numberProp(d, "substeps", 4));
    s.sleep = numberProp(d, "sleep", 1) != 0.0;
    s.continuous = numberProp(d, "continuous", 1) != 0.0;
    s.airDensity = numberProp(d, "airDensity", 1.225);
    s.jointHertz = numberProp(d, "jointHertz", 60);
    s.jointDamping = numberProp(d, "jointDamping", 2);
    s.coulombSoftening = numberProp(d, "coulombSoftening", 0.05);
    s.keyframeInterval = int(numberProp(d, "keyframeInterval", 240));
    s.hz = std::clamp(s.hz, 10, 2000);
    s.substeps = std::clamp(s.substeps, 1, 64);
    s.keyframeInterval = std::max(s.keyframeInterval, 1);
    return s;
}

World::World(const ItemDef& worldDef)
{
    settings_ = WorldSettings::fromDef(worldDef);
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {float(settings_.gravity.x), float(settings_.gravity.y)};
    wd.enableSleep = settings_.sleep;
    wd.enableContinuous = settings_.continuous;
    wd.workerCount = 1;
    id_ = b2CreateWorld(&wd);
    structureVersion_ = nextStructureVersion();
    b2BodyDef gd = b2DefaultBodyDef();
    gd.type = b2_staticBody;
    ground_ = b2CreateBody(id_, &gd);
    contactScratch_.resize(64);
}

World::~World()
{
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) destroyRuntime(*it);
    if (b2World_IsValid(id_)) b2DestroyWorld(id_);
}

void World::applySettings()
{
    b2World_SetGravity(id_, {float(settings_.gravity.x), float(settings_.gravity.y)});
    b2World_EnableSleeping(id_, settings_.sleep);
    b2World_EnableContinuous(id_, settings_.continuous);
}

void World::setWorldDef(const ItemDef& worldDef)
{
    settings_ = WorldSettings::fromDef(worldDef);
    applySettings();
    baselineValid_ = false;
}

const ItemDef* World::def(ItemId id) const
{
    const auto it = itemIndex_.find(id);
    return it == itemIndex_.end() ? nullptr : items_[it->second].def.get();
}

ItemRuntime* World::runtime(ItemId id) const
{
    const auto it = itemIndex_.find(id);
    return it == itemIndex_.end() ? nullptr : items_[it->second].rt.get();
}

void World::reindex()
{
    itemIndex_.clear();
    defs_.clear();
    for (size_t i = 0; i < items_.size(); ++i) {
        itemIndex_[items_[i].def->id] = i;
        defs_.push_back(items_[i].def);
    }
}

bool World::referencesSatisfied(const ItemDef& d) const
{
    for (ItemId r : d.type->references(d)) {
        const auto it = itemIndex_.find(r);
        if (it == itemIndex_.end() || !items_[it->second].rt) return false;
    }
    return true;
}

std::vector<size_t> World::dependentsOf(ItemId id) const
{
    std::vector<size_t> out;
    for (size_t i = 0; i < items_.size(); ++i) {
        const ItemDef& d = *items_[i].def;
        const auto refs = d.type->references(d);
        if (std::find(refs.begin(), refs.end(), id) != refs.end()) out.push_back(i);
    }
    return out;
}

void World::createRuntime(Entry& e, const RestoreState* restore)
{
    if (e.rt) return;
    if (!referencesSatisfied(*e.def)) return;
    e.rt = e.def->type->createRuntime(*e.def, *this, restore);
    if (e.rt && restore)
        if (const auto* cs = restore->customState(e.def->id)) e.rt->loadState(*cs);
}

void World::destroyRuntime(Entry& e)
{
    if (!e.rt) return;
    e.rt->destroy(*this);
    e.rt.reset();
}

void World::createDormant(const RestoreState* restore)
{
    // Creating one item can satisfy another's references; iterate to a fixed point.
    bool progress = true;
    while (progress) {
        progress = false;
        for (Entry& e : items_) {
            if (e.rt || e.def->type->kind() == ItemKind::Body) continue;
            createRuntime(e, restore);
            if (e.rt) progress = true;
        }
    }
}

void World::build(const std::vector<ItemDefPtr>& defs, const RestoreState* restore)
{
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) destroyRuntime(*it);
    items_.clear();
    constants_.clear();
    for (const ItemDefPtr& d : defs) items_.push_back({d, nullptr});
    reindex();
    for (Entry& e : items_)
        if (e.def->type->kind() == ItemKind::Body) createRuntime(e, restore);
    createDormant(restore);
    if (restore) {
        for (const auto& sf : restore->sleepIndex) {
            const int bi = bodyIndex(sf.id);
            if (bi < 0) continue;
            BodyRec& b = bodies_[size_t(bi)];
            b.forceCount = b.lastForceCount = int(std::min<uint32_t>(sf.count, kMaxForceEntries));
            std::copy_n(restore->sleepForces.begin() + sf.begin, b.forceCount, b.forces);
            std::copy_n(restore->sleepForces.begin() + sf.begin, b.forceCount, b.lastForces);
        }
    }
    structureVersion_ = nextStructureVersion();
    baselineValid_ = false;
}

void World::addItem(ItemDefPtr def, const RestoreState* restore)
{
    if (itemIndex_.count(def->id)) {
        updateItem(def, ~0ull);
        return;
    }
    items_.push_back({def, nullptr});
    reindex();
    createRuntime(items_.back(), restore);
    createDormant(restore);
    structureVersion_ = nextStructureVersion();
    baselineValid_ = false;
}

void World::removeItem(ItemId id)
{
    const auto it = itemIndex_.find(id);
    if (it == itemIndex_.end()) return;
    for (size_t di : dependentsOf(id)) destroyRuntime(items_[di]);
    const size_t idx = itemIndex_[id];
    destroyRuntime(items_[idx]);
    items_.erase(items_.begin() + long(idx));
    reindex();
    structureVersion_ = nextStructureVersion();
    baselineValid_ = false;
}

void World::updateItem(ItemDefPtr def, uint64_t changed)
{
    const auto it = itemIndex_.find(def->id);
    if (it == itemIndex_.end()) {
        addItem(def);
        return;
    }
    Entry& e = items_[it->second];
    e.def = def;
    baselineValid_ = false;
    if (e.rt && e.rt->applyEdit(*def, changed, *this)) return;

    // Rebuild the item and everything attached to it, preserving motion.
    RestoreState rs;
    capture(rs);
    // An edited initial condition (pose/velocity) must win over the live state.
    const auto& schemaDefs = def->type->schema().defs();
    bool stateEdited = false;
    for (size_t i = 0; i < schemaDefs.size(); ++i)
        if ((changed & propBit(int(i))) && schemaDefs[i].has(PF_State)) stateEdited = true;
    if (stateEdited) {
        rs.bodies.erase(std::remove_if(rs.bodies.begin(), rs.bodies.end(),
                                       [&](const auto& p) { return p.first == def->id; }),
                        rs.bodies.end());
    }
    const std::vector<size_t> deps = dependentsOf(def->id);
    for (size_t di : deps) destroyRuntime(items_[di]);
    destroyRuntime(e);
    createRuntime(e, &rs);
    for (size_t di : deps) createRuntime(items_[di], &rs);
    createDormant(&rs);
    structureVersion_ = nextStructureVersion();
}

void World::capture(RestoreState& out) const
{
    out.bodies.clear();
    out.custom.clear();
    out.sleepIndex.clear();
    out.sleepForces.clear();
    for (const BodyRec& b : bodies_) {
        if (!b.alive) continue;
        BodyState s;
        const b2Vec2 p = b2Body_GetPosition(b.id);
        const b2Rot q = b2Body_GetRotation(b.id);
        const b2Vec2 v = b2Body_GetLinearVelocity(b.id);
        s.px = p.x;
        s.py = p.y;
        s.qc = q.c;
        s.qs = q.s;
        s.vx = v.x;
        s.vy = v.y;
        s.w = b2Body_GetAngularVelocity(b.id);
        s.awake = b2Body_IsAwake(b.id) ? 1u : 0u;
        out.bodies.push_back({b.item, s});
        if (!s.awake && b.forceCount > 0) {
            out.sleepIndex.push_back({b.item, uint32_t(out.sleepForces.size()), uint32_t(b.forceCount)});
            out.sleepForces.insert(out.sleepForces.end(), b.forces, b.forces + b.forceCount);
        }
    }
    std::vector<double> buf;
    for (const Entry& e : items_) {
        if (!e.rt) continue;
        buf.clear();
        e.rt->saveState(buf);
        if (!buf.empty()) out.custom.push_back({e.def->id, buf});
    }
    out.sort();
}

int World::registerBody(ItemId item, b2BodyId id)
{
    int index;
    if (!freeBodies_.empty()) {
        index = freeBodies_.back();
        freeBodies_.pop_back();
        bodies_[size_t(index)] = BodyRec{};
    } else {
        index = int(bodies_.size());
        bodies_.emplace_back();
    }
    BodyRec& b = bodies_[size_t(index)];
    b.item = item;
    b.id = id;
    b.alive = true;
    b2Body_SetUserData(id, reinterpret_cast<void*>(intptr_t(index) + 1));
    bodyIndex_[item] = index;
    refreshMass(index);
    refreshBodyState(b);
    b.prevV = b.v;
    b.prevW = b.w;
    structureVersion_ = nextStructureVersion();
    return index;
}

void World::unregisterBody(int index)
{
    BodyRec& b = bodies_[size_t(index)];
    bodyIndex_.erase(b.item);
    b.alive = false;
    b.shapes.clear();
    b.shapeIds.clear();
    freeBodies_.push_back(index);
    structureVersion_ = nextStructureVersion();
}

int World::bodyIndex(ItemId item) const
{
    const auto it = bodyIndex_.find(item);
    return it == bodyIndex_.end() ? -1 : it->second;
}

b2BodyId World::bodyIdOr(ItemId item) const
{
    const int i = bodyIndex(item);
    return i >= 0 ? bodies_[size_t(i)].id : ground_;
}

void World::refreshMass(int index)
{
    BodyRec& b = bodies_[size_t(index)];
    b.type = uint8_t(b2Body_GetType(b.id));
    b.mass = b2Body_GetMass(b.id);
    b.invMass = b.mass > 0.0f ? 1.0f / b.mass : 0.0f;
    b.inertia = b2Body_GetRotationalInertia(b.id);
    b.gravityScale = b2Body_GetGravityScale(b.id);
    b.linearDamping = b2Body_GetLinearDamping(b.id);
    b.angularDamping = b2Body_GetAngularDamping(b.id);
}

void World::refreshBodyState(BodyRec& b)
{
    b.origin = b2Body_GetPosition(b.id);
    b.rot = b2Body_GetRotation(b.id);
    b.com = b2Body_GetWorldCenterOfMass(b.id);
    b.v = b2Body_GetLinearVelocity(b.id);
    b.w = b2Body_GetAngularVelocity(b.id);
    b.awake = b2Body_IsAwake(b.id);
}

static void addEntry(BodyRec& b, ForceSource source, float fx, float fy, float torque)
{
    const uint8_t s = uint8_t(source);
    for (int i = 0; i < b.forceCount; ++i) {
        if (b.forces[i].source == s) {
            b.forces[i].fx += fx;
            b.forces[i].fy += fy;
            b.forces[i].torque += torque;
            return;
        }
    }
    if (b.forceCount < kMaxForceEntries) {
        b.forces[b.forceCount++] = {fx, fy, torque, s};
    } else {
        // Table full: fold into the last entry (documented limitation).
        ForceEntry& e = b.forces[kMaxForceEntries - 1];
        e.fx += fx;
        e.fy += fy;
        e.torque += torque;
    }
}

void World::record(int index, ForceSource source, b2Vec2 f, float torque)
{
    addEntry(bodies_[size_t(index)], source, f.x, f.y, torque);
}

void World::addForce(int index, ForceSource source, b2Vec2 f, b2Vec2 point, bool wake)
{
    BodyRec& b = bodies_[size_t(index)];
    if (b.type != b2_dynamicBody) return;
    b2Body_ApplyForce(b.id, f, point, wake);
    const b2Vec2 r = b2Sub(point, b2Body_GetWorldCenterOfMass(b.id));
    addEntry(b, source, f.x, f.y, b2Cross(r, f));
}

void World::addForceAtCenter(int index, ForceSource source, b2Vec2 f, bool wake)
{
    BodyRec& b = bodies_[size_t(index)];
    if (b.type != b2_dynamicBody) return;
    b2Body_ApplyForceToCenter(b.id, f, wake);
    addEntry(b, source, f.x, f.y, 0.0f);
}

void World::addTorque(int index, ForceSource source, float t, bool wake)
{
    BodyRec& b = bodies_[size_t(index)];
    if (b.type != b2_dynamicBody) return;
    b2Body_ApplyTorque(b.id, t, wake);
    addEntry(b, source, 0.0f, 0.0f, t);
}

void World::setConstant(const QString& name, double value)
{
    for (auto& c : constants_) {
        if (c.first == name) {
            if (c.second != value) {
                c.second = value;
                ++constantsVersion_;
            }
            return;
        }
    }
    constants_.push_back({name, value});
    ++constantsVersion_;
}

void World::step()
{
    const float h = dt();
    const b2Vec2 g{float(settings_.gravity.x), float(settings_.gravity.y)};

    for (BodyRec& b : bodies_) {
        if (!b.alive) continue;
        b.prevV = b.v;
        b.prevW = b.w;
        // Sleeping bodies keep their last force breakdown (they are in equilibrium).
        // Bodies restored asleep from a keyframe have no contact data until they
        // wake, so they report no breakdown rather than a misleading partial one.
        b.refreshForces = b.awake;
        if (!b.refreshForces) continue;
        b.forceCount = 0;
        if (b.type != b2_dynamicBody) continue;
        addEntry(b, ForceSource::Gravity, b.mass * b.gravityScale * g.x, b.mass * b.gravityScale * g.y, 0.0f);
        if (b.linearDamping > 0.0f)
            addEntry(b, ForceSource::Damping, -b.mass * b.linearDamping * b.v.x, -b.mass * b.linearDamping * b.v.y,
                     -b.inertia * b.angularDamping * b.w);
        else if (b.angularDamping > 0.0f)
            addEntry(b, ForceSource::Damping, 0.0f, 0.0f, -b.inertia * b.angularDamping * b.w);
    }

    for (Entry& e : items_)
        if (e.rt) e.rt->preStep(*this);

    b2World_Step(id_, h, settings_.substeps);

    for (BodyRec& b : bodies_) {
        if (!b.alive) continue;
        refreshBodyState(b);
        b.a = b2MulSV(1.0f / h, b2Sub(b.v, b.prevV));
        b.alpha = (b.w - b.prevW) / h;
    }

    computeContactForces();

    for (Entry& e : items_)
        if (e.rt) e.rt->postStep(*this);

    time_ += double(h);
    ++step_;
}

void World::computeContactForces()
{
    const float invH = float(settings_.hz * settings_.substeps); // 1 / substep
    for (int bi = 0; bi < int(bodies_.size()); ++bi) {
        BodyRec& b = bodies_[size_t(bi)];
        if (!b.alive || b.type != b2_dynamicBody || !b.refreshForces) continue;
        if (!b.awake) {
            // Fell asleep during this step: Box2D no longer reports its contact
            // impulses, so keep the equilibrium breakdown of the last awake step.
            std::copy(b.lastForces, b.lastForces + b.lastForceCount, b.forces);
            b.forceCount = b.lastForceCount;
            continue;
        }
        const int cap = b2Body_GetContactCapacity(b.id);
        if (cap > int(contactScratch_.size())) contactScratch_.resize(size_t(cap) * 2); // rare growth
        const int n = b2Body_GetContactData(b.id, contactScratch_.data(), int(contactScratch_.size()));
        bool sliding = false;
        for (int ci = 0; ci < n; ++ci) {
            const b2ContactData& cd = contactScratch_[size_t(ci)];
            const b2Manifold& m = cd.manifold;
            if (m.pointCount == 0) continue;
            const b2BodyId bodyA = b2Shape_GetBody(cd.shapeIdA);
            const b2BodyId bodyB = b2Shape_GetBody(cd.shapeIdB);
            const bool isA = B2_ID_EQUALS(bodyA, b.id);
            const float sign = isA ? -1.0f : 1.0f;
            const b2Vec2 n2 = m.normal;
            const b2Vec2 t2 = b2RightPerp(n2);
            b2Vec2 fn{0, 0}, ft{0, 0};
            float tn = 0, tt = 0;
            // Conveyor surfaces: sliding is judged relative to the moving surface.
            float tangentSpeed = 0.0f;
            for (b2BodyId bid : {bodyA, bodyB}) {
                const intptr_t oi = reinterpret_cast<intptr_t>(b2Body_GetUserData(bid)) - 1;
                if (oi >= 0 && oi < intptr_t(bodies_.size())) tangentSpeed += bodies_[size_t(oi)].tangentSpeed;
            }
            for (int pi = 0; pi < m.pointCount; ++pi) {
                const b2ManifoldPoint& mp = m.points[pi];
                const b2Vec2 Pn = b2MulSV(sign * mp.normalImpulse * invH, n2);
                const b2Vec2 Pt = b2MulSV(sign * mp.tangentImpulse * invH, t2);
                const b2Vec2 r = b2Sub(mp.point, b.com);
                fn = b2Add(fn, Pn);
                ft = b2Add(ft, Pt);
                tn += b2Cross(r, Pn);
                tt += b2Cross(r, Pt);
                if (mp.normalImpulse > 0.0f) {
                    const b2Vec2 va = b2Body_GetWorldPointVelocity(bodyA, mp.point);
                    const b2Vec2 vb = b2Body_GetWorldPointVelocity(bodyB, mp.point);
                    const float vt = b2Dot(b2Sub(vb, va), t2) - tangentSpeed;
                    if (std::abs(vt) > 0.01f) sliding = true;
                }
            }
            addEntry(b, ForceSource::Normal, fn.x, fn.y, tn);
            addEntry(b, ForceSource::Friction, ft.x, ft.y, tt);
        }
        if (n > 0 || b.sliding) {
            if (sliding != b.sliding) {
                b.sliding = sliding;
                updateFrictionState(b);
            }
        }
        std::copy(b.forces, b.forces + b.forceCount, b.lastForces);
        b.lastForceCount = b.forceCount;
    }
}

void World::updateFrictionState(BodyRec& b)
{
    const float mu = b.sliding ? b.muKinetic : b.muStatic;
    for (b2ShapeId s : b.shapeIds)
        if (b2Shape_IsValid(s)) b2Shape_SetFriction(s, mu);
}

EnergySnap World::energy() const
{
    EnergySnap e;
    const b2Vec2 g{float(settings_.gravity.x), float(settings_.gravity.y)};
    for (const BodyRec& b : bodies_) {
        if (!b.alive || b.type != b2_dynamicBody) continue;
        e.kinetic += 0.5 * double(b.mass) * double(b2Dot(b.v, b.v)) + 0.5 * double(b.inertia) * double(b.w) * double(b.w);
        e.gravity -= double(b.mass) * double(b.gravityScale) * (double(g.x) * double(b.com.x) + double(g.y) * double(b.com.y));
    }
    for (const Entry& en : items_) {
        if (!en.rt) continue;
        const PotentialEnergy pe = en.rt->potentialEnergy(*this);
        e.spring += pe.spring;
        e.field += pe.field;
    }
    e.total = e.kinetic + e.gravity + e.spring + e.field;
    if (!baselineValid_) {
        baseline_ = e.total;
        baselineValid_ = true;
    }
    e.baseline = baseline_;
    return e;
}

void World::writeSnapshot(Snapshot& s) const
{
    s.step = step_;
    s.time = time_;
    s.dt = 1.0 / settings_.hz;
    s.structureVersion = structureVersion_;
    size_t n = 0;
    for (const BodyRec& b : bodies_)
        if (b.alive) ++n;
    s.bodies.resize(n);
    size_t k = 0;
    for (const BodyRec& b : bodies_) {
        if (!b.alive) continue;
        BodySnap& o = s.bodies[k++];
        o.id = b.item;
        o.awake = b.awake;
        o.bodyType = b.type;
        o.x = b.origin.x;
        o.y = b.origin.y;
        o.c = b.rot.c;
        o.s = b.rot.s;
        o.cx = b.com.x;
        o.cy = b.com.y;
        o.vx = b.v.x;
        o.vy = b.v.y;
        o.w = b.w;
        o.ax = b.a.x;
        o.ay = b.a.y;
        o.alpha = b.alpha;
        o.mass = b.mass;
        o.inertia = b.inertia;
        o.forceCount = uint8_t(b.forceCount);
        float fx = 0, fy = 0, t = 0;
        for (int i = 0; i < b.forceCount; ++i) {
            o.forces[i] = b.forces[i];
            fx += b.forces[i].fx;
            fy += b.forces[i].fy;
            t += b.forces[i].torque;
        }
        o.fx = fx;
        o.fy = fy;
        o.torque = t;
    }
    s.items.clear();
    for (const Entry& e : items_) {
        if (!e.rt) continue;
        ItemSnap is;
        is.id = e.def->id;
        if (e.rt->snapshot(*this, is)) s.items.push_back(is);
    }
    s.energy = energy();
}

} // namespace kn
