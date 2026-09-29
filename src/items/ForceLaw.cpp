// Force law: a force on body A given by expressions, either as components
// Fx, Fy or as a radial magnitude F(r) along the line from the centre (body B
// or a fixed point; positive pushes away). Radial laws can act equally and
// oppositely on B.
#include "ConnectorItem.h"
#include "EquationItem.h"

namespace kn {

class ForceLaw final : public ConnectorType {
public:
    enum Mode : int { Components, Radial };

    ForceLaw()
        : ConnectorType({QStringLiteral("equation.force"), QStringLiteral("Force law"), QStringLiteral("Equations"),
                         QStringLiteral("labplot-xy-equation-curve"), QStringLiteral(":/icons/equation.svg"),
                         {QStringLiteral("custom force"), QStringLiteral("central force"), QStringLiteral("formula"),
                          QStringLiteral("inverse square"), QStringLiteral("hooke")},
                         QStringLiteral("A force from a formula: click the body it acts on, then the centre (a body or a "
                                        "point).")})
    {
        meta_.kind = ItemKind::Equation;
        addAttachment(false);
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Force"));
        mode_ = s.size();
        s.choice(QStringLiteral("mode"), QStringLiteral("Form"), {QStringLiteral("Components Fx, Fy"), QStringLiteral("Radial F(r)")},
                 Radial);
        fx_ = s.size();
        s.text(QStringLiteral("fx"), QStringLiteral("Fx"), QStringLiteral("0"))
            .tip(QStringLiteral("Newtons. Variables: t, x, y, vx, vy, v (speed), m (mass), dx, dy (from the centre), "
                                "r (distance to the centre)."));
        fy_ = s.size();
        s.text(QStringLiteral("fy"), QStringLiteral("Fy"), QStringLiteral("-m*9.81"));
        fr_ = s.size();
        s.text(QStringLiteral("fr"), QStringLiteral("F(r) (outward)"), QStringLiteral("-50*(r - 2)"))
            .tip(QStringLiteral("Magnitude along the line from the centre; positive repels, negative attracts."));
        react_ = s.size();
        s.boolean(QStringLiteral("reaction"), QStringLiteral("Equal and opposite on B"), true);
        status_ = s.size();
        s.text(QStringLiteral("status"), QStringLiteral("Status"), QString()).readOnly();
        addAppearance(Color::fromRgb(0x00b894));
    }

    static const QStringList& vars()
    {
        static const QStringList v = {QStringLiteral("t"), QStringLiteral("x"),  QStringLiteral("y"),  QStringLiteral("vx"),
                                      QStringLiteral("vy"), QStringLiteral("v"), QStringLiteral("m"), QStringLiteral("dx"),
                                      QStringLiteral("dy"), QStringLiteral("r")};
        return v;
    }

    void propertyChanged(ItemDef& d, int) const override
    {
        QStringList errs;
        if (d.i(mode_) == Components) {
            for (const auto& [i, label] : {std::pair{fx_, "Fx"}, std::pair{fy_, "Fy"}}) {
                const QString e = checkExpr(QString::fromLatin1(label), d.s(i), vars());
                if (!e.isEmpty()) errs << e;
            }
        } else {
            const QString e = checkExpr(QStringLiteral("F(r)"), d.s(fr_), vars());
            if (!e.isEmpty()) errs << e;
        }
        d.set(status_, errs.join(QLatin1Char('\n')));
    }

    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Fx"), unit::N}, {5, QStringLiteral("Fy"), unit::N}, {6, QStringLiteral("|F|"), unit::N}};
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const Color col = color(d, ctx);
        if (d.i(mode_) == Radial) ctx.batch.dashed(Layer::Front, a, b, lineWidth(ctx, 1.2f), col.withAlpha(170));
        ctx.batch.disc(Layer::Front, a, ctx.px(4), col, col);
        if (d.i(mode_) == Radial) ctx.batch.circleOutline(Layer::Front, b, ctx.px(5), col, 1.5f);
        static const QString fLabel = QStringLiteral("F = "), fUnit = QStringLiteral(" N");
        if (ctx.snap) {
            const Vec2 f{ctx.snap->v[4], ctx.snap->v[5]};
            const double m = length(f);
            if (m > 1e-9) ctx.batch.arrow(Layer::Overlay, a, a + f * (ctx.px(50) / m), 2.0f, col, 9);
            ctx.batch.valueLabel(a, fLabel, m, fUnit, 1, ctx.theme.text, 10, 12);
            ctx.batch.labels.back().si = true;
            if (ctx.snap->v[7] > 0.5f) {
                static const QString bad = QStringLiteral("non-finite force (undefined constant?)");
                ctx.batch.label(a, bad, Color::fromRgb(0xda4453), 0, -18);
            }
        }
        if (!d.s(status_).isEmpty()) ctx.batch.label(a, d.s(status_), Color::fromRgb(0xda4453), 0, -18);
    }

    int mode_, fx_, fy_, fr_, react_, status_;
};

namespace {
class ForceRuntime final : public ConnectorRuntime {
public:
    ForceRuntime(const ItemDef& d, const ForceLaw& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        mode_ = int(d.i(t.mode_));
        reaction_ = d.b(t.react_);
        const QStringList& v = ForceLaw::vars();
        ok_ = mode_ == ForceLaw::Components ? compileExpr(d.s(t.fx_), v, w, fx_, nullptr) && compileExpr(d.s(t.fy_), v, w, fy_, nullptr)
                                            : compileExpr(d.s(t.fr_), v, w, fr_, nullptr);
    }
    bool applyEdit(const ItemDef&, uint64_t, World&) override { return false; } // recompile

    void preStep(World& w) override
    {
        force_ = {0, 0};
        bad_ = false;
        if (ia_ < 0 || !ok_) return;
        const BodyRec& b = w.body(ia_);
        if (b.type != b2_dynamicBody) return;
        const b2Vec2 c = anchorB(w);
        const double vars[10] = {w.time(), b.com.x, b.com.y, b.v.x, b.v.y, b2Length(b.v), b.mass,
                                 double(b.com.x) - c.x, double(b.com.y) - c.y, b2Distance(b.com, c)};
        const double* k = w.constantValues();
        double fx, fy;
        if (mode_ == ForceLaw::Components) {
            fx = fx_.eval(vars, k);
            fy = fy_.eval(vars, k);
        } else {
            const double r = vars[9], f = fr_.eval(vars, k);
            fx = r > 1e-12 ? f * vars[7] / r : 0.0;
            fy = r > 1e-12 ? f * vars[8] / r : 0.0;
        }
        if (!std::isfinite(fx) || !std::isfinite(fy)) {
            bad_ = true;
            return;
        }
        force_ = {float(fx), float(fy)};
        w.addForceAtCenter(ia_, ForceSource::Equation, force_, true);
        if (mode_ == ForceLaw::Radial && reaction_ && ib_ >= 0) w.addForce(ib_, ForceSource::Equation, b2Neg(force_), c, true);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = force_.x;
        s.v[5] = force_.y;
        s.v[6] = b2Length(force_);
        s.v[7] = bad_ || !ok_ ? 1.0f : 0.0f;
        return true;
    }

private:
    const ForceLaw& t_;
    expr::Program fx_, fy_, fr_;
    int mode_ = 0;
    bool reaction_ = true, ok_ = false, bad_ = false;
    b2Vec2 force_{0, 0};
};
} // namespace

std::unique_ptr<ItemRuntime> ForceLaw::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<ForceRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(ForceLaw)

} // namespace kn
