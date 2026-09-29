// Item system. Each item type (body, connector, field, fluid, optical element,
// tool, equation...) is one class deriving from ItemType, registered once in
// src/items/ItemList.h. The type declares metadata, a property schema, geometry,
// rendering, serialization hooks and how it creates its physics runtime.
#pragma once

#include "Property.h"

#include <QJsonObject>

#include <array>
#include <memory>
#include <vector>

namespace kn {

class World;
class ItemRuntime;
class RenderBatch;
struct DrawContext;
struct RestoreState;

namespace optics {
struct Scene;
}

// Fields a scene can display as a vector overlay.
enum class FieldKind : uint8_t { Gravity, Electric, Magnetic, Wind };

enum class ItemKind : uint8_t { World, Group, Body, Connector, Field, Fluid, Optics, Tool, Equation, Constant };
constexpr uint32_t kindBit(ItemKind k) { return 1u << uint32_t(k); }

enum class CreateMode : uint8_t {
    Place,   // click/drop places the item at a point
    Connect, // click body/point A, then body/point B
    Draw,    // freehand/polygon drawing tool
};

struct ItemMeta {
    QString typeId;   // stable id used in files, e.g. "body.box"
    QString name;     // toolbox label
    QString category; // toolbox section
    QString icon;     // freedesktop icon name for QIcon::fromTheme
    QString fallbackIcon; // bundled resource path used when the theme lacks `icon`
    QStringList tags; // extra search terms
    QString description;
    ItemKind kind = ItemKind::Body;
    CreateMode create = CreateMode::Place;
    bool toolbox = true;
};

class ItemType;

struct ItemDef {
    ItemId id = 0;
    const ItemType* type = nullptr;
    QString name;
    ItemId parent = 0; // group item id, 0 = root
    bool visible = true;
    bool locked = false;
    std::vector<PropValue> props;

    double d(int i) const { return std::get<double>(props[size_t(i)]); }
    int64_t i(int i) const { return std::get<int64_t>(props[size_t(i)]); }
    bool b(int i) const { return std::get<bool>(props[size_t(i)]); }
    Vec2 v(int i) const { return std::get<Vec2>(props[size_t(i)]); }
    Color c(int i) const { return std::get<Color>(props[size_t(i)]); }
    const QString& s(int i) const { return std::get<QString>(props[size_t(i)]); }
    const PointList& pts(int i) const { return std::get<PointList>(props[size_t(i)]); }
    ItemId ref(int i) const { return ItemId(std::get<int64_t>(props[size_t(i)])); }
    void set(int i, PropValue v) { props[size_t(i)] = std::move(v); }
};
using ItemDefPtr = std::shared_ptr<const ItemDef>;

// Local-space collision/picking primitive. Polygons are convex, CCW, <= 8 vertices.
struct Shape {
    enum Kind : uint8_t { Circle, Polygon, Capsule, Segment };
    Kind kind = Circle;
    Vec2 a;          // circle centre, capsule/segment first point
    Vec2 b;          // capsule/segment second point
    double radius = 0.0;
    int count = 0;
    std::array<Vec2, 8> verts{};

    static Shape circle(Vec2 c, double r) { Shape s; s.kind = Circle; s.a = c; s.radius = r; return s; }
    static Shape capsule(Vec2 a, Vec2 b, double r) { Shape s; s.kind = Capsule; s.a = a; s.b = b; s.radius = r; return s; }
    static Shape segment(Vec2 a, Vec2 b) { Shape s; s.kind = Segment; s.a = a; s.b = b; return s; }
    static Shape polygon(const Vec2* v, int n)
    {
        Shape s;
        s.kind = Polygon;
        s.count = std::min(n, 8);
        for (int i = 0; i < s.count; ++i) s.verts[size_t(i)] = v[i];
        return s;
    }
    static Shape box(double hw, double hh)
    {
        const Vec2 v[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        return polygon(v, 4);
    }
};
using ShapeList = std::vector<Shape>;

// Read access to the scene for drawing/picking items that depend on other items
// (e.g. a spring drawn between two bodies). Implemented by the viewport.
class SceneView {
public:
    virtual ~SceneView() = default;
    virtual const ItemDef* def(ItemId id) const = 0;
    // Current pose of a posed item: from the live snapshot when simulating,
    // otherwise from the document. Returns false for unknown ids.
    virtual bool pose(ItemId id, Pose& out) const = 0;
};

class ItemType {
public:
    virtual ~ItemType() = default;

    virtual const ItemMeta& meta() const = 0;
    virtual const PropertySchema& schema() const = 0;

    ItemKind kind() const { return meta().kind; }
    const QString& typeId() const { return meta().typeId; }

    // Creates a definition with schema defaults, placed at `at` (world metres).
    ItemDef makeDefault(ItemId id, Vec2 at) const;
    // Called after a property changes (index) or after loading (-1) so derived
    // properties (e.g. mass from density and area) stay consistent.
    virtual void propertyChanged(ItemDef& def, int index) const;

    // --- pose & geometry -------------------------------------------------------
    // Items with "position"/"angle" properties get these for free.
    int positionIndex() const;
    int angleIndex() const;
    virtual bool hasPose() const { return positionIndex() >= 0; }
    virtual Pose pose(const ItemDef& def) const;
    virtual void setPose(ItemDef& def, const Pose& p) const;
    virtual void translate(ItemDef& def, Vec2 delta) const;
    virtual void rotateAbout(ItemDef& def, double dAngle, Vec2 pivot) const;
    // Scale the item's size about its local origin. Returns false if unsupported.
    virtual bool scale(ItemDef& def, double sx, double sy) const;

    // Physical shapes in local space (bodies, regions, optical elements).
    virtual void shapes(const ItemDef& def, ShapeList& out) const;
    // World-space hit test used by the viewport. Default uses shapes().
    virtual bool hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const;
    virtual Aabb bounds(const ItemDef& def, const SceneView& view) const;

    // Connectors: called after the viewport filled bodyA/anchorA/bodyB/anchorB
    // (anchors are body-local, or world points when the body is 0) so the type
    // can derive lengths etc. from the current poses.
    virtual void initConnection(ItemDef&, const SceneView&) const {}

    // Live values an item publishes in ItemSnap::v (for graphs and probes).
    struct Channel {
        int index;      // into ItemSnap::v
        QString name;
        QString unit;
        double scale = 1.0; // display value = v * scale
    };
    virtual QList<Channel> channels() const { return {}; }

    // Items referenced by this item (connectors, attachments). Used for
    // dependency ordering, copy/paste remapping and deletion.
    std::vector<ItemId> references(const ItemDef& def) const;

    // --- serialization -----------------------------------------------------------
    virtual QJsonObject toJson(const ItemDef& def) const;
    // Fills props from JSON; unknown keys are ignored, missing keys keep defaults.
    virtual void fromJson(ItemDef& def, const QJsonObject& props, QStringList* warnings) const;

    // --- physics -------------------------------------------------------------------
    // Returns nullptr for items with no physics (groups, optics, constants...).
    virtual std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& world,
                                                       const RestoreState* restore) const;

    // --- fields and optics (UI side) -----------------------------------------------------
    // Adds this item's contribution to field `k` at world point p (gravity:
    // m/s², electric: V/m, magnetic: in-plane T, wind: m/s). `pose` is the
    // item's current pose. Gravity zones that cancel the world's gravity at p
    // return true (the caller subtracts the world gravity once per such item).
    virtual bool addField(const ItemDef&, const Pose&, FieldKind, Vec2 /*p*/, Vec2& /*inout*/) const { return false; }
    // Optical elements and light sources add their surfaces and rays.
    virtual void addOptics(const ItemDef&, const Pose&, optics::Scene&) const {}

    // --- rendering -------------------------------------------------------------------
    virtual void draw(const ItemDef& def, const DrawContext& ctx) const;

protected:
    mutable int posIndex_ = -2;
    mutable int angIndex_ = -2;
};

class ItemRegistry {
public:
    static ItemRegistry& instance();
    void add(const ItemType* t);
    const ItemType* find(const QString& typeId) const;
    const std::vector<const ItemType*>& types() const { return types_; }

private:
    ItemRegistry();
    std::vector<const ItemType*> types_;
};

// Helper for item source files: defines the factory the registry list refers to.
#define KINETIC_DEFINE_ITEM(Class)                    \
    const ::kn::ItemType* kineticItemFactory_##Class() \
    {                                                   \
        static const Class instance;                    \
        return &instance;                               \
    }

} // namespace kn
