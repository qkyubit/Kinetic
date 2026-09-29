#include "Item.h"

#include "Geometry.h"
#include "RenderBatch.h"
#include "Runtime.h"

namespace kn {

ItemDef ItemType::makeDefault(ItemId id, Vec2 at) const
{
    ItemDef d;
    d.id = id;
    d.type = this;
    d.name = QStringLiteral("%1 %2").arg(meta().name).arg(id);
    const auto& defs = schema().defs();
    d.props.reserve(defs.size());
    for (const PropDef& p : defs) d.props.push_back(p.def);
    if (positionIndex() >= 0) d.set(positionIndex(), at);
    propertyChanged(d, -1);
    return d;
}

void ItemType::propertyChanged(ItemDef&, int) const {}

int ItemType::positionIndex() const
{
    if (posIndex_ == -2) {
        const int i = schema().indexOf(QStringLiteral("position"));
        posIndex_ = (i >= 0 && schema()[i].type == PropType::Vector) ? i : -1;
    }
    return posIndex_;
}

int ItemType::angleIndex() const
{
    if (angIndex_ == -2) {
        const int i = schema().indexOf(QStringLiteral("angle"));
        angIndex_ = (i >= 0 && schema()[i].type == PropType::Double) ? i : -1;
    }
    return angIndex_;
}

Pose ItemType::pose(const ItemDef& def) const
{
    const int pi = positionIndex();
    const int ai = angleIndex();
    return Pose::fromAngle(pi >= 0 ? def.v(pi) : Vec2{}, ai >= 0 ? def.d(ai) : 0.0);
}

void ItemType::setPose(ItemDef& def, const Pose& p) const
{
    if (positionIndex() >= 0) def.set(positionIndex(), clampValue(schema()[positionIndex()], p.p));
    if (angleIndex() >= 0) def.set(angleIndex(), clampValue(schema()[angleIndex()], wrapAngle(p.angle())));
}

void ItemType::translate(ItemDef& def, Vec2 delta) const
{
    if (positionIndex() >= 0) def.set(positionIndex(), clampValue(schema()[positionIndex()], def.v(positionIndex()) + delta));
}

void ItemType::rotateAbout(ItemDef& def, double dAngle, Vec2 pivot) const
{
    if (positionIndex() < 0) return;
    const Vec2 p = def.v(positionIndex());
    def.set(positionIndex(), clampValue(schema()[positionIndex()], pivot + rotate(p - pivot, dAngle)));
    if (angleIndex() >= 0) def.set(angleIndex(), wrapAngle(def.d(angleIndex()) + dAngle));
}

bool ItemType::scale(ItemDef&, double, double) const { return false; }

void ItemType::shapes(const ItemDef&, ShapeList& out) const { out.clear(); }

bool ItemType::hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const
{
    Pose p = pose(def);
    view.pose(def.id, p);
    thread_local ShapeList shapesBuf;
    shapes(def, shapesBuf);
    if (shapesBuf.empty()) return hasPose() && distance(p.p, world) <= tolerance * 6.0;
    const Vec2 local = p.inverse(world);
    for (const Shape& s : shapesBuf)
        if (geom::shapeContains(s, local, tolerance)) return true;
    return false;
}

Aabb ItemType::bounds(const ItemDef& def, const SceneView& view) const
{
    Pose p = pose(def);
    view.pose(def.id, p);
    thread_local ShapeList shapesBuf;
    shapes(def, shapesBuf);
    Aabb b;
    for (const Shape& s : shapesBuf) b.add(geom::shapeBounds(s, p));
    if (!b.valid() && hasPose()) {
        b.add(p.p);
        b.inflate(0.1);
    }
    return b;
}

std::vector<ItemId> ItemType::references(const ItemDef& def) const
{
    std::vector<ItemId> out;
    const auto& defs = schema().defs();
    for (size_t i = 0; i < defs.size(); ++i)
        if (defs[i].type == PropType::ItemRef && def.ref(int(i)) != 0) out.push_back(def.ref(int(i)));
    return out;
}

QJsonObject ItemType::toJson(const ItemDef& def) const
{
    QJsonObject o;
    const auto& defs = schema().defs();
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].has(PF_Transient)) continue;
        o.insert(defs[i].key, valueToJson(defs[i], def.props[i]));
    }
    return o;
}

void ItemType::fromJson(ItemDef& def, const QJsonObject& props, QStringList* warnings) const
{
    const auto& defs = schema().defs();
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].has(PF_Transient)) continue;
        const auto it = props.find(defs[i].key);
        if (it == props.end()) continue;
        PropValue v;
        if (valueFromJson(defs[i], *it, v))
            def.props[i] = std::move(v);
        else if (warnings)
            *warnings << QStringLiteral("%1 \"%2\": invalid value for property '%3', using default")
                             .arg(meta().typeId, def.name, defs[i].key);
    }
    if (warnings) {
        for (auto it = props.begin(); it != props.end(); ++it)
            if (schema().indexOf(it.key()) < 0)
                *warnings << QStringLiteral("%1 \"%2\": unknown property '%3' ignored").arg(meta().typeId, def.name, it.key());
    }
    propertyChanged(def, -1);
}

std::unique_ptr<ItemRuntime> ItemType::createRuntime(const ItemDef&, World&, const RestoreState*) const
{
    return nullptr;
}

void ItemType::draw(const ItemDef& def, const DrawContext& ctx) const
{
    thread_local ShapeList shapesBuf;
    shapes(def, shapesBuf);
    const int ci = schema().indexOf(QStringLiteral("color"));
    const Color fill = ci >= 0 ? def.c(ci) : ctx.theme.dim;
    const Color outline = ctx.selected ? ctx.theme.highlight : ctx.theme.outline;
    for (const Shape& s : shapesBuf)
        ctx.batch.shape(Layer::Body, s, ctx.pose, fill, outline, ctx.selected ? 2.5f : 1.2f, false);
}

void ItemRegistry::add(const ItemType* t)
{
    Q_ASSERT(t && !find(t->typeId()));
    types_.push_back(t);
}

const ItemType* ItemRegistry::find(const QString& typeId) const
{
    for (const ItemType* t : types_)
        if (t->typeId() == typeId) return t;
    return nullptr;
}

ItemRegistry& ItemRegistry::instance()
{
    static ItemRegistry r;
    return r;
}

} // namespace kn
