// Shared base for connectors (springs, ropes, joints...) that attach to one or
// two bodies. End points are body-local anchors, or world points when the body
// reference is 0 ("world").
#pragma once

#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

class ConnectorType : public ItemType {
public:
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }
    bool hasPose() const override { return false; }
    void translate(ItemDef& def, Vec2 delta) const override;
    void rotateAbout(ItemDef& def, double dAngle, Vec2 pivot) const override;
    bool hitTest(const ItemDef& def, const SceneView& view, Vec2 world, double tolerance) const override;
    Aabb bounds(const ItemDef& def, const SceneView& view) const override;

    struct Idx {
        int bodyA = -1, anchorA = -1, bodyB = -1, anchorB = -1, collide = -1, color = -1;
    };
    const Idx& cidx() const { return cidx_; }

    // World positions of the end points, from the view's current poses.
    Vec2 worldA(const ItemDef& def, const SceneView& view) const;
    Vec2 worldB(const ItemDef& def, const SceneView& view) const;
    // Converts a world point to an anchor for `body` (local if body != 0).
    static Vec2 toAnchor(ItemId body, Vec2 world, const SceneView& view);
    // Extra world points (pulley wheels...) that move with the connector.
    virtual void extraPoints(const ItemDef&, std::vector<int>& vectorProps) const { vectorProps.clear(); }

protected:
    explicit ConnectorType(ItemMeta meta);
    void addAttachment(bool collideDefault);
    void addAppearance(Color c);
    Color color(const ItemDef& d, const DrawContext& ctx) const;
    float lineWidth(const DrawContext& ctx, float base) const { return ctx.selected ? base + 1.5f : base; }

    ItemMeta meta_;
    PropertySchema schema_;
    Idx cidx_;
};

// Physics side: resolves bodies and anchors once, then tracks world anchors.
class ConnectorRuntime : public ItemRuntime {
public:
    ConnectorRuntime(const ItemDef& def, const ConnectorType& type, World& world);
    void destroy(World& world) override;

protected:
    b2BodyId bodyA(const World& w) const { return ia_ >= 0 ? w.body(ia_).id : w.groundBody(); }
    b2BodyId bodyB(const World& w) const { return ib_ >= 0 ? w.body(ib_).id : w.groundBody(); }
    b2Vec2 anchorA(const World& w) const;
    b2Vec2 anchorB(const World& w) const;
    b2Vec2 velocityA(const World& w, b2Vec2 p) const;
    b2Vec2 velocityB(const World& w, b2Vec2 p) const;
    // Records the force/torque a Box2D joint applied, on both bodies.
    void recordJoint(World& w, ForceSource force, ForceSource torqueSource, float excludeTorque = 0.0f);
    // Applies an equal and opposite force pair at the anchors (force acts on B).
    void applyPair(World& w, ForceSource source, b2Vec2 forceOnB, b2Vec2 pA, b2Vec2 pB);
    void applyJointTuning(const World& w);
    // Fills v[0..3] with the anchors.
    void fillAnchors(const World& w, ItemSnap& s) const;

    int ia_ = -1, ib_ = -1; // body table indices, -1 = world
    b2Vec2 la_{0, 0}, lb_{0, 0};
    b2JointId joint_ = b2_nullJointId;
    // True when the Box2D joint was created with our B as its body A (so the
    // joint's reported force acts on our A).
    bool swapped_ = false;
};

// Drawing helpers.
void drawZigzag(RenderBatch& b, Layer l, Vec2 a, Vec2 c, int coils, double amplitude, float width, Color col);
void drawRope(RenderBatch& b, Layer l, Vec2 a, Vec2 c, double length, float width, Color col);
void drawPin(RenderBatch& b, Vec2 p, double pxPerM, Color fill, Color edge);

} // namespace kn
