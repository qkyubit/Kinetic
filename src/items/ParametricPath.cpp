// Parametric path: a kinematic body whose position (x(t), y(t)) and angle(t)
// are expressions of time, relative to the item's origin. It pushes other
// bodies it meets (when it has a radius) and can leave a trail.
#include "EquationItem.h"

namespace kn {

class ParametricPath final : public ItemType {
public:
    ParametricPath()
    {
        meta_ = {QStringLiteral("equation.path"), QStringLiteral("Parametric path"), QStringLiteral("Equations"),
                 QStringLiteral("draw-bezier-curves"), QStringLiteral(":/icons/equation.svg"),
                 {QStringLiteral("trajectory"), QStringLiteral("lissajous"), QStringLiteral("kinematic"),
                  QStringLiteral("x(t)"), QStringLiteral("function")},
                 QStringLiteral("A body that follows x(t), y(t) exactly; it pushes whatever is in the way."),
                 ItemKind::Equation};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Transform"));
        s.vec(QStringLiteral("position"), QStringLiteral("Origin"), unit::m, -10000, 10000, {0, 0});
        s.beginGroup(QStringLiteral("Path"));
        x_ = s.size();
        s.text(QStringLiteral("x"), QStringLiteral("x(t)"), QStringLiteral("2*cos(t)"))
            .tip(QStringLiteral("Metres from the origin. Use t (seconds), pi, e, functions such as sin, sqrt, "
                                "min, clamp, and the names of Constant items."));
        y_ = s.size();
        s.text(QStringLiteral("y"), QStringLiteral("y(t)"), QStringLiteral("sin(2*t)"));
        a_ = s.size();
        s.text(QStringLiteral("angleExpr"), QStringLiteral("angle(t) (rad)"), QStringLiteral("0"));
        preview_ = s.size();
        s.num(QStringLiteral("preview"), QStringLiteral("Preview duration"), unit::s, 0, 1000, 2 * kPi, 2);
        status_ = s.size();
        s.text(QStringLiteral("status"), QStringLiteral("Status"), QString()).readOnly();
        s.beginGroup(QStringLiteral("Body"));
        r_ = s.size();
        s.num(QStringLiteral("radius"), QStringLiteral("Radius (0: no collision)"), unit::m, 0, 100, 0.2, 3);
        color_ = s.size();
        s.colour(QStringLiteral("color"), QStringLiteral("Colour"), Color::fromRgb(0x00b894));
        addTrailProperties(s, true, Color::fromRgb(0x00b894));
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

    static const QStringList& vars()
    {
        static const QStringList v = {QStringLiteral("t")};
        return v;
    }

    void propertyChanged(ItemDef& d, int) const override
    {
        QStringList errs;
        for (const auto& [i, label] : {std::pair{x_, "x(t)"}, std::pair{y_, "y(t)"}, std::pair{a_, "angle(t)"}}) {
            const QString e = checkExpr(QString::fromLatin1(label), d.s(i), vars());
            if (!e.isEmpty()) errs << e;
        }
        d.set(status_, errs.isEmpty() ? QString() : errs.join(QLatin1Char('\n')));
    }

    void shapes(const ItemDef& d, ShapeList& out) const override
    {
        out.clear();
        out.push_back(Shape::circle(startOffset(d), std::max(d.d(r_), 0.1)));
    }
    Vec2 startOffset(const ItemDef& d) const
    {
        const expr::Program* px = cachedExpr(d.s(x_), vars());
        const expr::Program* py = cachedExpr(d.s(y_), vars());
        const double t = 0.0;
        Vec2 p{px ? px->eval(&t, nullptr) : 0.0, py ? py->eval(&t, nullptr) : 0.0};
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) p = {};
        return p;
    }

    QList<Channel> channels() const override
    {
        return {{0, QStringLiteral("x"), unit::m}, {1, QStringLiteral("y"), unit::m}, {2, QStringLiteral("Angle"), unit::deg, kDegPerRad}};
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState* restore) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = d.c(color_);
        const Color edge = ctx.selected ? ctx.theme.highlight : Color{uint8_t(c.r * 0.55), uint8_t(c.g * 0.55), uint8_t(c.b * 0.55), 255};
        const Vec2 o = ctx.pose.p;
        // Path preview (constants are unknown in the editor, so previews using them are skipped).
        const expr::Program* px = cachedExpr(d.s(x_), vars());
        const expr::Program* py = cachedExpr(d.s(y_), vars());
        if (px && py && (!ctx.simActive || ctx.selected)) {
            const int n = 240;
            const double T = d.d(preview_);
            Vec2 prev;
            for (int i = 0; i <= n; ++i) {
                const double t = T * i / n;
                const Vec2 p = o + Vec2{px->eval(&t, nullptr), py->eval(&t, nullptr)};
                if (i > 0 && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(prev.x) && std::isfinite(prev.y))
                    ctx.batch.line(Layer::Back, prev, p, 1.0f, c.withAlpha(110));
                prev = p;
            }
        }
        Vec2 at = o + startOffset(d);
        double angle = 0.0;
        if (ctx.snap) {
            at = {ctx.snap->v[0], ctx.snap->v[1]};
            angle = ctx.snap->v[2];
        }
        const double r = d.d(r_) > 0 ? d.d(r_) : ctx.px(5);
        ctx.batch.disc(Layer::Body, at, r, c, edge, ctx.selected ? 2.5f : 1.5f, d.d(r_) > 0, angle);
        ctx.batch.disc(Layer::Front, o, ctx.px(2.5), edge, edge);
        if (!d.s(status_).isEmpty()) ctx.batch.label(at, d.s(status_), Color::fromRgb(0xda4453), 0, -18);
        else if (ctx.snap && ctx.snap->v[3] > 0.5f) {
            static const QString bad = QStringLiteral("non-finite value (undefined constant?)");
            ctx.batch.label(at, bad, Color::fromRgb(0xda4453), 0, -18);
        }
    }

    int x_, y_, a_, preview_, status_, r_, color_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class PathRuntime final : public ItemRuntime {
public:
    PathRuntime(const ItemDef& d, const ParametricPath& t, World& w, const RestoreState* restore) : ItemRuntime(d.id), t_(t)
    {
        origin_ = t.pose(d).p;
        const QStringList& v = ParametricPath::vars();
        ok_ = compileExpr(d.s(t.x_), v, w, x_, nullptr) && compileExpr(d.s(t.y_), v, w, y_, nullptr) &&
              compileExpr(d.s(t.a_), v, w, a_, nullptr);
        Vec2 p = origin_;
        double angle = 0;
        evalAt(w, w.time(), p, angle);
        body_.create(w, d.id, p, angle, d.d(t.r_), restore);
    }
    void destroy(World& w) override { body_.destroy(w); }
    bool applyEdit(const ItemDef&, uint64_t, World&) override { return false; }

    bool evalAt(const World& w, double t, Vec2& p, double& angle) const
    {
        if (!ok_) return false;
        const double* c = w.constantValues();
        const double x = x_.eval(&t, c), y = y_.eval(&t, c), a = a_.eval(&t, c);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(a)) return false;
        p = origin_ + Vec2{x, y};
        angle = a;
        return true;
    }

    void preStep(World& w) override
    {
        Vec2 p;
        double angle;
        bad_ = !evalAt(w, w.time() + w.dt(), p, angle);
        if (bad_)
            body_.hold(w);
        else
            body_.driveTo(w, p, angle);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        const Vec2 p = body_.position(w);
        s.v[0] = float(p.x);
        s.v[1] = float(p.y);
        s.v[2] = float(body_.angle(w));
        s.v[3] = bad_ || !ok_ ? 1.0f : 0.0f;
        return true;
    }

private:
    const ParametricPath& t_;
    Vec2 origin_;
    expr::Program x_, y_, a_;
    bool ok_ = false, bad_ = false;
    MarkerBody body_;
};
} // namespace

std::unique_ptr<ItemRuntime> ParametricPath::createRuntime(const ItemDef& d, World& w, const RestoreState* restore) const
{
    return std::make_unique<PathRuntime>(d, *this, w, restore);
}

KINETIC_DEFINE_ITEM(ParametricPath)

} // namespace kn
