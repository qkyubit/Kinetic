#include "ConnectorItem.h"

#include "core/Geometry.h"

namespace kn {

ConnectorType::ConnectorType(ItemMeta meta) : meta_(std::move(meta))
{
    meta_.kind = ItemKind::Connector;
    meta_.create = CreateMode::Connect;
}

void ConnectorType::addAttachment(bool collideDefault)
{
    auto& s = schema_;
    s.beginGroup(QStringLiteral("Attachment"));
    cidx_.bodyA = s.size();
    s.ref(QStringLiteral("bodyA"), QStringLiteral("Body A"), kindBit(ItemKind::Body));
    cidx_.anchorA = s.size();
    s.vec(QStringLiteral("anchorA"), QStringLiteral("Anchor A"), unit::m, -10000, 10000, {0, 0})
        .rebuild()
        .tip(QStringLiteral("In body A's local frame, or a world point when attached to the world."));
    cidx_.bodyB = s.size();
    s.ref(QStringLiteral("bodyB"), QStringLiteral("Body B"), kindBit(ItemKind::Body));
    cidx_.anchorB = s.size();
    s.vec(QStringLiteral("anchorB"), QStringLiteral("Anchor B"), unit::m, -10000, 10000, {0, 0})
        .rebuild()
        .tip(QStringLiteral("In body B's local frame, or a world point when attached to the world."));
    cidx_.collide = s.size();
    s.boolean(QStringLiteral("collideConnected"), QStringLiteral("Connected bodies collide"), collideDefault).rebuild();
}

void ConnectorType::addAppearance(Color c)
{
    schema_.beginGroup(QStringLiteral("Appearance"));
    cidx_.color = schema_.size();
    schema_.colour(QStringLiteral("color"), QStringLiteral("Colour"), c);
}

Color ConnectorType::color(const ItemDef& d, const DrawContext& ctx) const
{
    return ctx.selected ? ctx.theme.highlight : (cidx_.color >= 0 ? d.c(cidx_.color) : ctx.theme.outline);
}

Vec2 ConnectorType::toAnchor(ItemId body, Vec2 world, const SceneView& view)
{
    Pose p;
    if (body && view.pose(body, p)) return p.inverse(world);
    return world;
}

Vec2 ConnectorType::worldA(const ItemDef& def, const SceneView& view) const
{
    Pose p;
    const ItemId b = def.ref(cidx_.bodyA);
    if (b && view.pose(b, p)) return p.apply(def.v(cidx_.anchorA));
    return def.v(cidx_.anchorA);
}

Vec2 ConnectorType::worldB(const ItemDef& def, const SceneView& view) const
{
    Pose p;
    const ItemId b = def.ref(cidx_.bodyB);
    if (b && view.pose(b, p)) return p.apply(def.v(cidx_.anchorB));
    return def.v(cidx_.anchorB);
}

void ConnectorType::translate(ItemDef& def, Vec2 delta) const
{
    if (!def.ref(cidx_.bodyA)) def.set(cidx_.anchorA, def.v(cidx_.anchorA) + delta);
    if (!def.ref(cidx_.bodyB)) def.set(cidx_.anchorB, def.v(cidx_.anchorB) + delta);
    std::vector<int> extra;
    extraPoints(def, extra);
    for (int i : extra) def.set(i, def.v(i) + delta);
}

void ConnectorType::rotateAbout(ItemDef& def, double dAngle, Vec2 pivot) const
{
    auto rot = [&](int i) { def.set(i, pivot + rotate(def.v(i) - pivot, dAngle)); };
    if (!def.ref(cidx_.bodyA)) rot(cidx_.anchorA);
    if (!def.ref(cidx_.bodyB)) rot(cidx_.anchorB);
    std::vector<int> extra;
    extraPoints(def, extra);
    for (int i : extra) rot(i);
}

bool ConnectorType::hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const
{
    return geom::distanceToSegment(world, worldA(def, view), worldB(def, view)) <= tolerance * 2.0;
}

Aabb ConnectorType::bounds(const ItemDef& def, const SceneView& view) const
{
    Aabb b;
    b.add(worldA(def, view));
    b.add(worldB(def, view));
    std::vector<int> extra;
    extraPoints(def, extra);
    for (int i : extra) b.add(def.v(i));
    b.inflate(0.05);
    return b;
}

// --- runtime -------------------------------------------------------------------------------

ConnectorRuntime::ConnectorRuntime(const ItemDef& def, const ConnectorType& type, World& world) : ItemRuntime(def.id)
{
    const auto& ix = type.cidx();
    ia_ = def.ref(ix.bodyA) ? world.bodyIndex(def.ref(ix.bodyA)) : -1;
    ib_ = def.ref(ix.bodyB) ? world.bodyIndex(def.ref(ix.bodyB)) : -1;
    const Vec2 a = def.v(ix.anchorA), b = def.v(ix.anchorB);
    la_ = {float(a.x), float(a.y)};
    lb_ = {float(b.x), float(b.y)};
}

void ConnectorRuntime::destroy(World&)
{
    if (b2Joint_IsValid(joint_)) b2DestroyJoint(joint_);
    joint_ = b2_nullJointId;
}

b2Vec2 ConnectorRuntime::anchorA(const World& w) const
{
    if (ia_ < 0) return la_;
    const BodyRec& b = w.body(ia_);
    return b2TransformPoint(b2Transform{b.origin, b.rot}, la_);
}

b2Vec2 ConnectorRuntime::anchorB(const World& w) const
{
    if (ib_ < 0) return lb_;
    const BodyRec& b = w.body(ib_);
    return b2TransformPoint(b2Transform{b.origin, b.rot}, lb_);
}

b2Vec2 ConnectorRuntime::velocityA(const World& w, b2Vec2 p) const
{
    if (ia_ < 0) return {0, 0};
    const BodyRec& b = w.body(ia_);
    return b2Add(b.v, b2CrossSV(b.w, b2Sub(p, b.com)));
}

b2Vec2 ConnectorRuntime::velocityB(const World& w, b2Vec2 p) const
{
    if (ib_ < 0) return {0, 0};
    const BodyRec& b = w.body(ib_);
    return b2Add(b.v, b2CrossSV(b.w, b2Sub(p, b.com)));
}

void ConnectorRuntime::applyPair(World& w, ForceSource source, b2Vec2 f, b2Vec2 pA, b2Vec2 pB)
{
    if (ib_ >= 0) w.addForce(ib_, source, f, pB);
    if (ia_ >= 0) w.addForce(ia_, source, b2Neg(f), pA);
}

void ConnectorRuntime::recordJoint(World& w, ForceSource force, ForceSource torqueSource, float excludeTorque)
{
    if (!b2Joint_IsValid(joint_)) return;
    // Box2D reports the force and torque on its body B; with swapped_ that is our A.
    const float sign = swapped_ ? -1.0f : 1.0f;
    const b2Vec2 f = b2MulSV(sign, b2Joint_GetConstraintForce(joint_));
    const float t = sign * (b2Joint_GetConstraintTorque(joint_) - excludeTorque);
    const b2Vec2 pA = anchorA(w), pB = anchorB(w);
    if (ib_ >= 0) {
        const BodyRec& b = w.body(ib_);
        w.record(ib_, force, f, b2Cross(b2Sub(pB, b.com), f));
        if (t != 0.0f) w.record(ib_, torqueSource, {0, 0}, t);
    }
    if (ia_ >= 0) {
        const BodyRec& a = w.body(ia_);
        w.record(ia_, force, b2Neg(f), b2Cross(b2Sub(pA, a.com), b2Neg(f)));
        if (t != 0.0f) w.record(ia_, torqueSource, {0, 0}, -t);
    }
}

void ConnectorRuntime::applyJointTuning(const World& w)
{
    if (b2Joint_IsValid(joint_))
        b2Joint_SetConstraintTuning(joint_, float(w.settings().jointHertz), float(w.settings().jointDamping));
}

void ConnectorRuntime::fillAnchors(const World& w, ItemSnap& s) const
{
    const b2Vec2 a = anchorA(w), b = anchorB(w);
    s.v[0] = a.x;
    s.v[1] = a.y;
    s.v[2] = b.x;
    s.v[3] = b.y;
}

// --- drawing helpers -------------------------------------------------------------------------

void drawZigzag(RenderBatch& b, Layer l, Vec2 a, Vec2 c, int coils, double amplitude, float width, Color col)
{
    const double len = distance(a, c);
    if (len < 1e-9) return;
    const Vec2 u = (c - a) / len;
    const Vec2 n = perp(u) * amplitude;
    const double lead = std::min(0.15 * len, 0.2);
    const Vec2 s = a + u * lead;
    const Vec2 e = c - u * lead;
    b.line(l, a, s, width, col);
    b.line(l, e, c, width, col);
    const int segs = std::max(2, coils * 2);
    Vec2 prev = s;
    for (int i = 1; i <= segs; ++i) {
        const double t = double(i) / (segs + 1);
        const Vec2 p = lerp(s, e, t) + n * ((i % 2) ? 1.0 : -1.0);
        b.line(l, prev, p, width, col);
        prev = p;
    }
    b.line(l, prev, e, width, col);
}

void drawRope(RenderBatch& b, Layer l, Vec2 a, Vec2 c, double length, float width, Color col)
{
    const double d = distance(a, c);
    if (d >= length * 0.999 || d < 1e-9) {
        b.line(l, a, c, width, col);
        return;
    }
    // Slack rope: a parabola with the rope's arc length (approximately),
    // sagging along -y (gravity direction in the default world).
    const double sag = std::sqrt(std::max(0.0, 3.0 * d * (length - d) / 8.0));
    const int n = 24;
    Vec2 prev = a;
    for (int i = 1; i <= n; ++i) {
        const double t = double(i) / n;
        const Vec2 p = lerp(a, c, t) - Vec2{0, 4.0 * sag * t * (1.0 - t)};
        b.line(l, prev, p, width, col);
        prev = p;
    }
}

void drawPin(RenderBatch& b, Vec2 p, double pxPerM, Color fill, Color edge)
{
    b.disc(Layer::Front, p, 4.5 / pxPerM, fill, edge, 1.5f);
    b.disc(Layer::Front, p, 1.5 / pxPerM, edge, edge, 0.0f);
}

} // namespace kn
