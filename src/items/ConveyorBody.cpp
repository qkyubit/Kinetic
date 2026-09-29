// Conveyor belt: a static block whose surface moves at a set speed (Box2D
// tangent speed). Positive speed moves objects on top towards +x.
#include "BoxBody.h"

#include "core/RenderBatch.h"
#include "core/World.h"

namespace kn {

class ConveyorBody final : public BoxBody {
public:
    ConveyorBody()
        : BoxBody({QStringLiteral("mechanism.conveyor"), QStringLiteral("Conveyor"), QStringLiteral("Mechanisms"),
                   QStringLiteral("media-playlist-repeat"), QStringLiteral(":/icons/tool.svg"),
                   {QStringLiteral("belt"), QStringLiteral("treadmill"), QStringLiteral("surface velocity")},
                   QStringLiteral("Static belt whose surface moves; the top surface moves towards +x for positive "
                                  "speeds.")},
                  Defaults{6.0, 0.4, 3, 0, Color::fromRgb(0x34495e)})
    {
        schema_.beginGroup(QStringLiteral("Conveyor"));
        speed_ = schema_.size();
        schema_.num(QStringLiteral("surfaceSpeed"), QStringLiteral("Surface speed"), unit::mps, -50, 50, 2, 3);
    }

    void customizeShape(const ItemDef& d, b2ShapeDef& sd) const override
    {
        sd.material.tangentSpeed = float(d.d(speed_));
    }
    void customizeBody(const ItemDef& d, World& w, int bi) const override
    {
        w.body(bi).tangentSpeed = float(d.d(speed_));
    }
    bool applyExtraEdit(const ItemDef& d, int index, World& w, int bi) const override
    {
        if (index != speed_) return BoxBody::applyExtraEdit(d, index, w, bi);
        BodyRec& rec = w.body(bi);
        rec.tangentSpeed = float(d.d(speed_));
        for (b2ShapeId s : rec.shapeIds) {
            b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(s);
            m.tangentSpeed = rec.tangentSpeed;
            b2Shape_SetSurfaceMaterial(s, m);
        }
        return true;
    }

    void drawExtras(const ItemDef& d, const DrawContext& ctx) const override
    {
        // Chevrons along the belt, moving with the surface speed.
        const double w = d.d(width_), h = d.d(height_), v = d.d(speed_);
        const double spacing = std::max(0.25, 30.0 / ctx.pxPerM);
        const double offset = std::fmod(ctx.time * v, spacing) + (v < 0 ? spacing : 0.0);
        const Color c = d.c(idx_.color);
        const Color mark{uint8_t(255 - (255 - c.r) / 3), uint8_t(255 - (255 - c.g) / 3), uint8_t(255 - (255 - c.b) / 3), 200};
        const double dir = v >= 0 ? 1.0 : -1.0;
        const double a = std::min(h * 0.3, spacing * 0.3);
        for (double x = -w / 2 + std::fmod(offset, spacing); x < w / 2 - a; x += spacing) {
            const Vec2 p0 = ctx.pose.apply({x - dir * a, a}), p1 = ctx.pose.apply({x, 0}), p2 = ctx.pose.apply({x - dir * a, -a});
            ctx.batch.line(Layer::Body, p0, p1, 1.5f, mark);
            ctx.batch.line(Layer::Body, p1, p2, 1.5f, mark);
        }
    }

private:
    int speed_ = -1;
};

KINETIC_DEFINE_ITEM(ConveyorBody)

} // namespace kn
