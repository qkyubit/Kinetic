// Shared base for field items (gravity zones, wind, electric and magnetic
// fields): a pose plus a region that is the whole world, a rectangle or a
// circle. Bodies are inside when their centre of mass is.
#pragma once

#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

class FieldType : public ItemType {
public:
    enum Region : int { WholeWorld, Rectangle, Circle };

    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }
    void shapes(const ItemDef& def, ShapeList& out) const override;
    bool scale(ItemDef& def, double sx, double sy) const override;
    bool hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const override;

    bool contains(const ItemDef& def, const Pose& pose, Vec2 p) const;
    int regionIndex() const { return region_; }

protected:
    explicit FieldType(ItemMeta meta);
    void addRegion(int defaultRegion, double w, double h, double r);
    void addColour(Color c);
    // Region outline and tint, plus the item's marker; returns the colour used.
    Color drawRegion(const ItemDef& def, const DrawContext& ctx) const;
    // Geometry and appearance edits are handled by rebuilding.
    ItemMeta meta_;
    PropertySchema schema_;
    int region_ = -1, w_ = -1, h_ = -1, r_ = -1, color_ = -1;
};

// Runtime helper: region test against a pose fixed at creation.
class FieldRuntime : public ItemRuntime {
public:
    FieldRuntime(const ItemDef& def, const FieldType& type, World& world);
    bool applyEdit(const ItemDef& def, uint64_t changed, World& world) override;

protected:
    // Region test for body `i` (alive, dynamic, awake).
    bool affects(const World& w, int i) const;
    virtual void edited(const ItemDef&) {}

    const FieldType* type_;
    ItemDefPtr def_;
    int region_ = 0;
    Pose pose_;
    double hw_ = 0, hh_ = 0, r2_ = 0;
};

} // namespace kn
