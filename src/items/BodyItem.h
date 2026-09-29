// Shared base for rigid-body item types. Concrete body types add their shape
// properties and implement shapes(); everything else (mass/density coupling,
// materials, Box2D creation, live edits, drawing) lives here.
#pragma once

#include "core/Item.h"
#include "core/Runtime.h"

namespace kn {

struct Material {
    const char* name;
    double density;     // kg/m³
    double muStatic;
    double muKinetic;
    double restitution;
};

// Index 0 is "Custom". Values: engineering handbook figures for dry contact.
const std::vector<Material>& materialPresets();

class BodyItemType : public ItemType {
public:
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }
    void propertyChanged(ItemDef& def, int index) const override;
    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& world,
                                               const RestoreState* restore) const override;
    void draw(const ItemDef& def, const DrawContext& ctx) const override;
    // Charged bodies are point charges at their origin for the field overlay.
    bool addField(const ItemDef& def, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override;

    // Outline drawn around the body (defaults to each shape's own outline).
    virtual bool outline(const ItemDef&, PointList&) const { return false; }
    // Hooks for specialised bodies (conveyors, magnets...).
    virtual void customizeShape(const ItemDef&, b2ShapeDef&) const {}
    virtual void customizeBody(const ItemDef&, World&, int /*bodyIndex*/) const {}
    // Live edit of a subclass property. Return false to request a rebuild.
    virtual bool applyExtraEdit(const ItemDef& def, int index, World& world, int bodyIndex) const;
    virtual void drawExtras(const ItemDef&, const DrawContext&) const {}

    // Mass properties from the current shapes and density (Box2D formulas, so the
    // editor shows exactly what the simulation uses).
    struct MassInfo {
        double mass = 0, inertia = 0, area = 0;
        Vec2 center;
    };
    MassInfo massInfo(const ItemDef& def) const;

    struct Indices {
        int position = -1, angle = -1, bodyType = -1, material = -1, density = -1, mass = -1, inertia = -1;
        int muStatic = -1, muKinetic = -1, restitution = -1, linearDamping = -1, angularDamping = -1;
        int gravityScale = -1, fixedRotation = -1, bullet = -1, velocity = -1, angularVelocity = -1;
        int layers = -1, mask = -1, charge = -1, dragCoefficient = -1, color = -1, label = -1;
        int trail = -1, trailLength = -1, trailColor = -1, trailFade = -1, showForces = -1;
    };
    const Indices& idx() const { return idx_; }

protected:
    explicit BodyItemType(ItemMeta meta);
    void addTransform();
    // Adds the common body properties. Call after the shape properties.
    void addBodyProperties(int defaultMaterial, int defaultBodyType, Color defaultColor);

    ItemMeta meta_;
    PropertySchema schema_;
    Indices idx_;
};

} // namespace kn
