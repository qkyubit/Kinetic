#include "BodyItem.h"

#include "core/Friction.h"
#include "core/Geometry.h"
#include "core/RenderBatch.h"
#include "core/World.h"

namespace kn {

const std::vector<Material>& materialPresets()
{
    // density kg/m³, static μ, kinetic μ, restitution
    static const std::vector<Material> m = {
        {"Custom", 1000, 0.6, 0.5, 0.2},
        {"Steel", 7850, 0.74, 0.57, 0.60},
        {"Wood", 700, 0.50, 0.30, 0.45},
        {"Rubber", 1100, 1.00, 0.80, 0.80},
        {"Ice", 917, 0.10, 0.03, 0.30},
        {"Glass", 2500, 0.94, 0.40, 0.70},
        {"Concrete", 2400, 0.62, 0.50, 0.20},
    };
    return m;
}

namespace {
QStringList materialNames()
{
    QStringList l;
    for (const Material& m : materialPresets()) l << QString::fromLatin1(m.name);
    return l;
}

class BodyRuntime final : public ItemRuntime {
public:
    BodyRuntime(ItemId id, const BodyItemType* type, int index) : ItemRuntime(id), type_(type), index_(index) {}
    int bodyIndex() const override { return index_; }

    void destroy(World& w) override
    {
        const b2BodyId bid = w.body(index_).id;
        w.unregisterBody(index_);
        if (b2Body_IsValid(bid)) b2DestroyBody(bid);
    }

    bool applyEdit(const ItemDef& def, uint64_t changed, World& w) override
    {
        const auto& ix = type_->idx();
        BodyRec& rec = w.body(index_);
        const b2BodyId bid = rec.id;
        bool massDirty = false;
        bool poseSet = false;
        const int n = type_->schema().size();
        for (int i = 0; i < n && i < 63; ++i) {
            if (!(changed & propBit(i))) continue;
            if (i == ix.position || i == ix.angle) {
                if (!poseSet) {
                    const Vec2 p = def.v(ix.position);
                    b2Body_SetTransform(bid, {float(p.x), float(p.y)}, b2MakeRot(float(def.d(ix.angle))));
                    const Vec2 v = def.v(ix.velocity);
                    b2Body_SetLinearVelocity(bid, {float(v.x), float(v.y)});
                    b2Body_SetAngularVelocity(bid, float(def.d(ix.angularVelocity)));
                    b2Body_SetAwake(bid, true);
                    poseSet = true;
                }
            } else if (i == ix.velocity) {
                const Vec2 v = def.v(ix.velocity);
                b2Body_SetLinearVelocity(bid, {float(v.x), float(v.y)});
                b2Body_SetAwake(bid, true);
            } else if (i == ix.angularVelocity) {
                b2Body_SetAngularVelocity(bid, float(def.d(ix.angularVelocity)));
                b2Body_SetAwake(bid, true);
            } else if (i == ix.bodyType) {
                b2Body_SetType(bid, b2BodyType(def.i(ix.bodyType)));
                massDirty = true;
            } else if (i == ix.density || i == ix.mass) {
                for (b2ShapeId s : rec.shapeIds) b2Shape_SetDensity(s, float(def.d(ix.density)), false);
                b2Body_ApplyMassFromShapes(bid);
                massDirty = true;
            } else if (i == ix.muStatic || i == ix.muKinetic || i == ix.material) {
                rec.muStatic = float(def.d(ix.muStatic));
                rec.muKinetic = float(def.d(ix.muKinetic));
                const int materialId = friction::materialId(rec.muStatic, rec.muKinetic);
                for (b2ShapeId s : rec.shapeIds) {
                    b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(s);
                    m.friction = rec.sliding ? rec.muKinetic : rec.muStatic;
                    m.restitution = float(def.d(ix.restitution));
                    m.userMaterialId = materialId;
                    b2Shape_SetSurfaceMaterial(s, m);
                }
                if (i == ix.material) {
                    for (b2ShapeId s : rec.shapeIds) b2Shape_SetDensity(s, float(def.d(ix.density)), false);
                    b2Body_ApplyMassFromShapes(bid);
                    massDirty = true;
                }
                b2Body_SetAwake(bid, true);
            } else if (i == ix.restitution) {
                for (b2ShapeId s : rec.shapeIds) b2Shape_SetRestitution(s, float(def.d(ix.restitution)));
            } else if (i == ix.linearDamping) {
                b2Body_SetLinearDamping(bid, float(def.d(ix.linearDamping)));
                massDirty = true;
            } else if (i == ix.angularDamping) {
                b2Body_SetAngularDamping(bid, float(def.d(ix.angularDamping)));
                massDirty = true;
            } else if (i == ix.gravityScale) {
                b2Body_SetGravityScale(bid, float(def.d(ix.gravityScale)));
                b2Body_SetAwake(bid, true);
                massDirty = true;
            } else if (i == ix.fixedRotation) {
                b2Body_SetFixedRotation(bid, def.b(ix.fixedRotation));
                massDirty = true;
            } else if (i == ix.bullet) {
                b2Body_SetBullet(bid, def.b(ix.bullet));
            } else if (i == ix.layers || i == ix.mask) {
                b2Filter f = b2DefaultFilter();
                f.categoryBits = uint64_t(def.i(ix.layers));
                f.maskBits = uint64_t(def.i(ix.mask));
                for (b2ShapeId s : rec.shapeIds) b2Shape_SetFilter(s, f);
            } else if (i == ix.charge) {
                rec.charge = float(def.d(ix.charge));
            } else if (i == ix.dragCoefficient) {
                rec.dragCoefficient = float(def.d(ix.dragCoefficient));
            } else if (i == ix.inertia || i == ix.color || i == ix.label || i == ix.trail || i == ix.trailLength ||
                       i == ix.trailColor || i == ix.trailFade || i == ix.showForces) {
                // Presentation only.
            } else if (!type_->applyExtraEdit(def, i, w, index_)) {
                return false;
            }
        }
        if (changed & (1ull << 63)) return false;
        if (massDirty) w.refreshMass(index_);
        return true;
    }

private:
    const BodyItemType* type_;
    int index_;
};
} // namespace

BodyItemType::BodyItemType(ItemMeta meta) : meta_(std::move(meta)) { meta_.kind = ItemKind::Body; }

void BodyItemType::addTransform()
{
    schema_.beginGroup(QStringLiteral("Transform"));
    idx_.position = schema_.size();
    schema_.vec(QStringLiteral("position"), QStringLiteral("Position"), unit::m, -10000, 10000, {0, 0}).state();
    idx_.angle = schema_.size();
    schema_.angle(QStringLiteral("angle"), QStringLiteral("Angle"), -180, 180, 0).state();
    schema_.beginGroup(QStringLiteral("Shape"));
}

void BodyItemType::addBodyProperties(int defaultMaterial, int defaultBodyType, Color defaultColor)
{
    const Material& mat = materialPresets()[size_t(defaultMaterial)];
    auto& s = schema_;
    s.beginGroup(QStringLiteral("Body"));
    idx_.bodyType = s.size();
    s.choice(QStringLiteral("bodyType"), QStringLiteral("Body type"),
             {QStringLiteral("Static"), QStringLiteral("Kinematic"), QStringLiteral("Dynamic")}, defaultBodyType)
        .tip(QStringLiteral("Static bodies never move; kinematic bodies move at their initial velocity; "
                            "dynamic bodies respond to forces."));

    s.beginGroup(QStringLiteral("Material"));
    idx_.material = s.size();
    s.choice(QStringLiteral("material"), QStringLiteral("Preset"), materialNames(), defaultMaterial);
    idx_.density = s.size();
    s.num(QStringLiteral("density"), QStringLiteral("Density"), unit::density, 1, 25000, mat.density, 1)
        .log()
        .tip(QStringLiteral("Mass per volume. The 2D world has an implicit thickness of 1 m."));
    idx_.mass = s.size();
    s.num(QStringLiteral("mass"), QStringLiteral("Mass"), unit::kg, 1e-4, 1e7, 1, 4)
        .log()
        .transient()
        .tip(QStringLiteral("Editing the mass updates the density (the shape keeps its size)."));
    idx_.inertia = s.size();
    s.num(QStringLiteral("inertia"), QStringLiteral("Moment of inertia"), unit::kgm2, 0, 1e12, 0, 4).readOnly();
    idx_.muStatic = s.size();
    s.num(QStringLiteral("staticFriction"), QStringLiteral("Static friction"), unit::none, 0, 2, mat.muStatic, 3);
    idx_.muKinetic = s.size();
    s.num(QStringLiteral("kineticFriction"), QStringLiteral("Kinetic friction"), unit::none, 0, 2, mat.muKinetic, 3);
    idx_.restitution = s.size();
    s.num(QStringLiteral("restitution"), QStringLiteral("Restitution"), unit::none, 0, 1, mat.restitution, 3);
    idx_.linearDamping = s.size();
    s.num(QStringLiteral("linearDamping"), QStringLiteral("Linear damping"), unit::persec, 0, 20, 0, 3);
    idx_.angularDamping = s.size();
    s.num(QStringLiteral("angularDamping"), QStringLiteral("Angular damping"), unit::persec, 0, 20, 0, 3);
    idx_.gravityScale = s.size();
    s.num(QStringLiteral("gravityScale"), QStringLiteral("Gravity scale"), unit::none, -5, 5, 1, 2);
    idx_.fixedRotation = s.size();
    s.boolean(QStringLiteral("fixedRotation"), QStringLiteral("Fixed rotation"), false);
    idx_.bullet = s.size();
    s.boolean(QStringLiteral("bullet"), QStringLiteral("Continuous collision (bullet)"), false);
    idx_.dragCoefficient = s.size();
    s.num(QStringLiteral("dragCoefficient"), QStringLiteral("Drag coefficient"), unit::none, 0, 3, 1.0, 2)
        .tip(QStringLiteral("Quadratic drag coefficient used by fluids and wind."));

    s.beginGroup(QStringLiteral("Initial state"));
    idx_.velocity = s.size();
    s.vec(QStringLiteral("velocity"), QStringLiteral("Velocity"), unit::mps, -1000, 1000, {0, 0}).state();
    idx_.angularVelocity = s.size();
    s.num(QStringLiteral("angularVelocity"), QStringLiteral("Angular velocity"), unit::radps, -200, 200, 0, 3).state();

    s.beginGroup(QStringLiteral("Collision"));
    idx_.layers = s.size();
    s.bits(QStringLiteral("layers"), QStringLiteral("Layers"), 16, 1)
        .tip(QStringLiteral("Collision layers this body belongs to."));
    idx_.mask = s.size();
    s.bits(QStringLiteral("mask"), QStringLiteral("Collides with"), 16, 0xFFFF)
        .tip(QStringLiteral("Layers this body collides with. Two bodies collide only if each one's mask "
                            "contains a layer of the other."));

    s.beginGroup(QStringLiteral("Electromagnetism"));
    idx_.charge = s.size();
    auto& ch = s.num(QStringLiteral("charge"), QStringLiteral("Charge"), QStringLiteral("µC"), -1e-3, 1e-3, 0, 3);
    ch.scale = 1e6;

    s.beginGroup(QStringLiteral("Appearance"));
    idx_.color = s.size();
    s.colour(QStringLiteral("color"), QStringLiteral("Colour"), defaultColor);
    idx_.label = s.size();
    s.text(QStringLiteral("label"), QStringLiteral("Label"), QString());

    s.beginGroup(QStringLiteral("Analysis"));
    idx_.trail = s.size();
    s.boolean(QStringLiteral("trail"), QStringLiteral("Show trail"), false);
    idx_.trailLength = s.size();
    s.num(QStringLiteral("trailLength"), QStringLiteral("Trail length"), unit::s, 0.1, 60, 5, 1);
    idx_.trailColor = s.size();
    s.colour(QStringLiteral("trailColor"), QStringLiteral("Trail colour"), Color::fromRgb(0xe67e22));
    idx_.trailFade = s.size();
    s.boolean(QStringLiteral("trailFade"), QStringLiteral("Fade trail"), true);
    idx_.showForces = s.size();
    s.boolean(QStringLiteral("showForces"), QStringLiteral("Show force vectors"), false);
}

BodyItemType::MassInfo BodyItemType::massInfo(const ItemDef& def) const
{
    MassInfo info;
    thread_local ShapeList list;
    shapes(def, list);
    const float density = float(def.d(idx_.density));
    double mass = 0.0, iOrigin = 0.0;
    Vec2 moment;
    for (const Shape& s : list) {
        b2MassData md{};
        switch (s.kind) {
        case Shape::Circle: {
            b2Circle c{{float(s.a.x), float(s.a.y)}, float(s.radius)};
            md = b2ComputeCircleMass(&c, density);
            break;
        }
        case Shape::Capsule: {
            b2Capsule c{{float(s.a.x), float(s.a.y)}, {float(s.b.x), float(s.b.y)}, float(s.radius)};
            md = b2ComputeCapsuleMass(&c, density);
            break;
        }
        case Shape::Polygon: {
            b2Vec2 pts[8];
            for (int i = 0; i < s.count; ++i) pts[i] = {float(s.verts[size_t(i)].x), float(s.verts[size_t(i)].y)};
            const b2Hull hull = b2ComputeHull(pts, s.count);
            if (hull.count == 0) continue;
            const b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            md = b2ComputePolygonMass(&poly, density);
            break;
        }
        case Shape::Segment: continue;
        }
        info.area += geom::shapeArea(s);
        mass += md.mass;
        const Vec2 c{md.center.x, md.center.y};
        moment += c * double(md.mass);
        iOrigin += double(md.rotationalInertia) + double(md.mass) * lengthSq(c);
    }
    info.mass = mass;
    if (mass > 0.0) {
        info.center = moment / mass;
        info.inertia = iOrigin - mass * lengthSq(info.center);
    }
    return info;
}

void BodyItemType::propertyChanged(ItemDef& def, int index) const
{
    const auto& presets = materialPresets();
    if (index == idx_.material) {
        const int64_t m = def.i(idx_.material);
        if (m > 0 && m < int64_t(presets.size())) {
            const Material& p = presets[size_t(m)];
            def.set(idx_.density, p.density);
            def.set(idx_.muStatic, p.muStatic);
            def.set(idx_.muKinetic, p.muKinetic);
            def.set(idx_.restitution, p.restitution);
        }
    } else if (index == idx_.mass) {
        const MassInfo mi = massInfo(def);
        const double wanted = def.d(idx_.mass);
        const double currentDensity = def.d(idx_.density);
        if (mi.mass > 0.0) {
            const double d = currentDensity * wanted / mi.mass;
            def.set(idx_.density, clampValue(schema_[idx_.density], d));
        }
        def.set(idx_.material, int64_t(0));
    } else if (index == idx_.density || index == idx_.muStatic || index == idx_.muKinetic ||
               index == idx_.restitution) {
        def.set(idx_.material, int64_t(0));
    }
    const MassInfo mi = massInfo(def);
    def.set(idx_.mass, mi.mass);
    def.set(idx_.inertia, mi.inertia);
}

std::unique_ptr<ItemRuntime> BodyItemType::createRuntime(const ItemDef& def, World& world,
                                                         const RestoreState* restore) const
{
    thread_local ShapeList list;
    shapes(def, list);
    if (list.empty()) return nullptr;

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2BodyType(def.i(idx_.bodyType));
    const Vec2 p = def.v(idx_.position);
    bd.position = {float(p.x), float(p.y)};
    bd.rotation = b2MakeRot(float(def.d(idx_.angle)));
    const Vec2 v = def.v(idx_.velocity);
    bd.linearVelocity = {float(v.x), float(v.y)};
    bd.angularVelocity = float(def.d(idx_.angularVelocity));
    bd.linearDamping = float(def.d(idx_.linearDamping));
    bd.angularDamping = float(def.d(idx_.angularDamping));
    bd.gravityScale = float(def.d(idx_.gravityScale));
    bd.fixedRotation = def.b(idx_.fixedRotation);
    bd.isBullet = def.b(idx_.bullet);
    if (const BodyState* st = restore ? restore->body(def.id) : nullptr) {
        bd.position = {st->px, st->py};
        bd.rotation = {st->qc, st->qs};
        bd.linearVelocity = {st->vx, st->vy};
        bd.angularVelocity = st->w;
        bd.isAwake = st->awake != 0;
    }
    const b2BodyId bid = b2CreateBody(world.id(), &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = float(def.d(idx_.density));
    sd.material.friction = float(def.d(idx_.muStatic));
    sd.material.restitution = float(def.d(idx_.restitution));
    sd.material.userMaterialId = friction::materialId(float(def.d(idx_.muStatic)), float(def.d(idx_.muKinetic)));
    sd.filter.categoryBits = uint64_t(def.i(idx_.layers));
    sd.filter.maskBits = uint64_t(def.i(idx_.mask));
    sd.enableContactEvents = false;
    sd.enableSensorEvents = true; // trigger zones see this body
    sd.updateBodyMass = false;
    customizeShape(def, sd);

    std::vector<b2ShapeId> ids;
    std::vector<Shape> kept;
    for (const Shape& s : list) {
        b2ShapeId sid = b2_nullShapeId;
        switch (s.kind) {
        case Shape::Circle: {
            b2Circle c{{float(s.a.x), float(s.a.y)}, float(s.radius)};
            sid = b2CreateCircleShape(bid, &sd, &c);
            break;
        }
        case Shape::Capsule: {
            b2Capsule c{{float(s.a.x), float(s.a.y)}, {float(s.b.x), float(s.b.y)}, float(s.radius)};
            sid = b2CreateCapsuleShape(bid, &sd, &c);
            break;
        }
        case Shape::Segment: {
            b2Segment sg{{float(s.a.x), float(s.a.y)}, {float(s.b.x), float(s.b.y)}};
            sid = b2CreateSegmentShape(bid, &sd, &sg);
            break;
        }
        case Shape::Polygon: {
            b2Vec2 pts[8];
            for (int i = 0; i < s.count; ++i) pts[i] = {float(s.verts[size_t(i)].x), float(s.verts[size_t(i)].y)};
            const b2Hull hull = b2ComputeHull(pts, s.count);
            if (hull.count == 0) break; // degenerate piece (smaller than Box2D's linear slop)
            const b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            sid = b2CreatePolygonShape(bid, &sd, &poly);
            break;
        }
        }
        if (B2_IS_NON_NULL(sid)) {
            ids.push_back(sid);
            kept.push_back(s);
        }
    }
    if (ids.empty()) {
        b2DestroyBody(bid);
        return nullptr;
    }
    b2Body_ApplyMassFromShapes(bid);

    const int index = world.registerBody(def.id, bid);
    BodyRec& rec = world.body(index);
    rec.shapes = std::move(kept);
    rec.shapeIds = std::move(ids);
    rec.muStatic = float(def.d(idx_.muStatic));
    rec.muKinetic = float(def.d(idx_.muKinetic));
    rec.charge = float(def.d(idx_.charge));
    rec.dragCoefficient = float(def.d(idx_.dragCoefficient));
    customizeBody(def, world, index);
    return std::make_unique<BodyRuntime>(def.id, this, index);
}

bool BodyItemType::applyExtraEdit(const ItemDef&, int index, World&, int) const
{
    return !schema_[index].has(PF_Rebuild);
}

void BodyItemType::draw(const ItemDef& def, const DrawContext& ctx) const
{
    thread_local ShapeList list;
    shapes(def, list);
    Color fill = def.c(idx_.color);
    // Outline: a darker shade of the fill reads well on light and dark canvases.
    const Color edge = ctx.selected ? ctx.theme.highlight
                                    : Color{uint8_t(fill.r * 0.55), uint8_t(fill.g * 0.55), uint8_t(fill.b * 0.55), 255};
    if (ctx.body && !ctx.body->awake && ctx.body->bodyType == b2_dynamicBody) fill = fill.withAlpha(uint8_t(fill.a * 0.8));
    const float w = ctx.selected ? 2.5f : 1.5f;
    thread_local PointList ol;
    const bool customOutline = outline(def, ol);
    for (const Shape& s : list) {
        if (customOutline && s.kind == Shape::Polygon) {
            Vec2 wv[8];
            for (int i = 0; i < s.count; ++i) wv[i] = ctx.pose.apply(s.verts[size_t(i)]);
            ctx.batch.convexFill(Layer::Body, wv, s.count, fill);
        } else {
            ctx.batch.shape(Layer::Body, s, ctx.pose, fill, edge, w, true);
        }
    }
    if (customOutline) {
        thread_local PointList world;
        world.resize(ol.size());
        for (size_t i = 0; i < ol.size(); ++i) world[i] = ctx.pose.apply(ol[i]);
        ctx.batch.polyline(Layer::Body, world.data(), int(world.size()), true, w, edge);
    }
    if (!def.s(idx_.label).isEmpty()) ctx.batch.label(ctx.pose.p, def.s(idx_.label), ctx.theme.text);
    if (const double q = def.d(idx_.charge); q != 0.0) {
        // Charge sign badge.
        const Color c = q > 0 ? Color::fromRgb(0xc0392b) : Color::fromRgb(0x2e86de);
        const double r = ctx.px(7);
        ctx.batch.disc(Layer::Front, ctx.pose.p, r, Color{255, 255, 255, 230}, c, 1.5f);
        ctx.batch.line(Layer::Front, ctx.pose.p - Vec2{r * 0.6, 0}, ctx.pose.p + Vec2{r * 0.6, 0}, 2.0f, c);
        if (q > 0) ctx.batch.line(Layer::Front, ctx.pose.p - Vec2{0, r * 0.6}, ctx.pose.p + Vec2{0, r * 0.6}, 2.0f, c);
    }
    drawExtras(def, ctx);
}

bool BodyItemType::addField(const ItemDef& def, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const
{
    const double q = def.d(idx_.charge);
    if (k != FieldKind::Electric || q == 0.0) return false;
    const Vec2 r = p - pose.p;
    const double d2 = dot(r, r) + 1e-4;
    inout += r * (8.9875517923e9 * q / (d2 * std::sqrt(d2)));
    return false;
}

} // namespace kn
