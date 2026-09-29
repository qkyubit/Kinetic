// Shared base for optical elements and light sources. Optics are traced on the
// UI side (see core/Optics.h and app/OpticsView); the items have no physics.
#pragma once

#include "core/Item.h"
#include "core/Optics.h"
#include "core/RenderBatch.h"

namespace kn {

class OpticsType : public ItemType {
public:
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }
    void propertyChanged(ItemDef& def, int index) const override;
    void shapes(const ItemDef& def, ShapeList& out) const override;
    bool hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const override;
    void draw(const ItemDef& def, const DrawContext& ctx) const override;

    // Local-space outline for drawing and picking.
    virtual void outline(const ItemDef& def, PointList& out, bool& closed) const = 0;
    optics::Glass glass(const ItemDef& def) const;
    bool refractive() const { return glass_ >= 0; }

protected:
    explicit OpticsType(ItemMeta meta);
    void addGlass(int defaultPreset);
    void addColour(Color c);
    // Adds the closed outline as refracting segments (polygonal elements).
    void addPolygonElement(const ItemDef& def, const Pose& pose, optics::Scene& scene) const;

    ItemMeta meta_;
    PropertySchema schema_;
    int glass_ = -1, cauchyA_ = -1, cauchyB_ = -1, indexD_ = -1, color_ = -1;
};

// Arc helpers for spherical surfaces along the local x axis: the surface with
// vertex at x = vx, signed radius R (centre at vx + R), clipped to |y| <= h.
double sag(double R, double y);
void addSphericalSurface(optics::Scene& scene, optics::SurfaceKind kind, const Pose& pose, double vx, double R, double h,
                         int element);
void sampleSphericalSurface(PointList& out, double vx, double R, double h, bool downwards, int samples = 24);

} // namespace kn
