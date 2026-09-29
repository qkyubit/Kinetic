// Arbitrary (possibly concave) outline drawn by hand or by clicking vertices.
// The outline is simplified when drawn; for physics it is decomposed into convex
// pieces of at most 8 vertices (Box2D's polygon limit).
#include "BodyItem.h"

#include "core/Geometry.h"

#include <cstring>
#include <unordered_map>

namespace kn {

class FreehandBody final : public BodyItemType {
public:
    FreehandBody()
        : BodyItemType({QStringLiteral("body.freehand"), QStringLiteral("Custom shape"), QStringLiteral("Bodies"),
                        QStringLiteral("draw-freehand"), QStringLiteral(":/icons/body.svg"),
                        {QStringLiteral("draw"), QStringLiteral("freehand"), QStringLiteral("concave"),
                         QStringLiteral("polygon"), QStringLiteral("sketch")},
                        QStringLiteral("Draw any outline; it is decomposed into convex pieces."),
                        ItemKind::Body, CreateMode::Draw})
    {
        addTransform();
        points_ = schema_.size();
        // Default outline: an L-shaped piece, so a dropped item is visibly concave.
        schema_.points(QStringLiteral("outline"), QStringLiteral("Outline")).def =
            PointList{{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.0}, {0.0, 0.0}, {0.0, 0.5}, {-0.5, 0.5}};
        addBodyProperties(2, 2, Color::fromRgb(0xe74c3c));
    }

    bool outline(const ItemDef& def, PointList& out) const override
    {
        out = def.pts(points_);
        return out.size() >= 3;
    }

    void shapes(const ItemDef& def, ShapeList& out) const override
    {
        out.clear();
        const PointList& pts = def.pts(points_);
        if (pts.size() < 3) return;
        // Decomposition is cached per outline: shapes() runs every frame.
        thread_local std::unordered_map<uint64_t, std::pair<PointList, ShapeList>> cache;
        const uint64_t key = hashPoints(pts);
        auto it = cache.find(key);
        if (it == cache.end() || it->second.first != pts) {
            if (cache.size() > 256) cache.clear();
            ShapeList pieces;
            for (const PointList& piece : geom::convexDecompose(pts, 8))
                pieces.push_back(Shape::polygon(piece.data(), int(piece.size())));
            it = cache.insert_or_assign(key, std::make_pair(pts, std::move(pieces))).first;
        }
        out = it->second.second;
    }

    bool scale(ItemDef& def, double sx, double sy) const override
    {
        PointList pts = def.pts(points_);
        for (Vec2& p : pts) p = {p.x * sx, p.y * sy};
        if (sx * sy < 0) std::reverse(pts.begin(), pts.end());
        def.set(points_, std::move(pts));
        propertyChanged(def, points_);
        return true;
    }

private:
    static uint64_t hashPoints(const PointList& pts)
    {
        uint64_t h = 1469598103934665603ull;
        for (const Vec2& p : pts) {
            for (double d : {p.x, p.y}) {
                uint64_t bits;
                std::memcpy(&bits, &d, 8);
                h = (h ^ bits) * 1099511628211ull;
            }
        }
        return h ^ pts.size();
    }
    int points_ = -1;
};

KINETIC_DEFINE_ITEM(FreehandBody)

} // namespace kn
