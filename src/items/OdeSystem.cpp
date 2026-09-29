// ODE system: up to four state variables with derivatives given by
// expressions, integrated with classic RK4 at the physics rate in lockstep
// with the simulation. The variables are graphable channels; two of them can
// drive a marker (with a trail) to draw a phase portrait or a trajectory.
#include "EquationItem.h"

namespace kn {

class OdeSystem final : public ItemType {
public:
    static constexpr int kVars = 4;

    OdeSystem()
    {
        meta_ = {QStringLiteral("equation.ode"), QStringLiteral("ODE system"), QStringLiteral("Equations"),
                 QStringLiteral("office-chart-line"), QStringLiteral(":/icons/equation.svg"),
                 {QStringLiteral("differential equation"), QStringLiteral("runge kutta"), QStringLiteral("phase portrait"),
                  QStringLiteral("oscillator"), QStringLiteral("lorenz")},
                 QStringLiteral("Integrates dx/dt = f(t, x, ...) with RK4 alongside the simulation."), ItemKind::Equation};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Transform"));
        s.vec(QStringLiteral("position"), QStringLiteral("Plot origin"), unit::m, -10000, 10000, {0, 0});
        const char* names[kVars] = {"x", "v", "", ""};
        const char* rates[kVars] = {"v", "-4*x - 0.1*v", "0", "0"};
        const double initial[kVars] = {2, 0, 0, 0};
        s.beginGroup(QStringLiteral("Equations"));
        count_ = s.size();
        s.integer(QStringLiteral("count"), QStringLiteral("Variables"), 1, kVars, 2);
        for (int i = 0; i < kVars; ++i) {
            name_[i] = s.size();
            s.text(QStringLiteral("name%1").arg(i + 1), QStringLiteral("Name %1").arg(i + 1), QString::fromLatin1(names[i]));
            init_[i] = s.size();
            s.num(QStringLiteral("initial%1").arg(i + 1), QStringLiteral("Initial value %1").arg(i + 1), unit::none, -1e12,
                  1e12, initial[i], 4);
            rate_[i] = s.size();
            s.text(QStringLiteral("rate%1").arg(i + 1), QStringLiteral("d/dt of %1").arg(i + 1), QString::fromLatin1(rates[i]))
                .tip(QStringLiteral("Uses t, the variable names and Constant names."));
        }
        status_ = s.size();
        s.text(QStringLiteral("status"), QStringLiteral("Status"), QString()).readOnly();
        s.beginGroup(QStringLiteral("Plot"));
        QStringList axes{QStringLiteral("None"), QStringLiteral("t")};
        for (int i = 0; i < kVars; ++i) axes << QStringLiteral("Variable %1").arg(i + 1);
        px_ = s.size();
        s.choice(QStringLiteral("plotX"), QStringLiteral("Marker x"), axes, 2);
        py_ = s.size();
        s.choice(QStringLiteral("plotY"), QStringLiteral("Marker y"), axes, 3);
        scale_ = s.size();
        s.num(QStringLiteral("scale"), QStringLiteral("Metres per unit"), unit::m, 1e-9, 1e9, 1.0, 4).log();
        color_ = s.size();
        s.colour(QStringLiteral("color"), QStringLiteral("Colour"), Color::fromRgb(0x6c5ce7));
        addTrailProperties(s, true, Color::fromRgb(0x6c5ce7));
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

    int count(const ItemDef& d) const { return int(std::clamp<int64_t>(d.i(count_), 1, kVars)); }
    QStringList vars(const ItemDef& d) const
    {
        QStringList v{QStringLiteral("t")};
        for (int i = 0; i < count(d); ++i) v << d.s(name_[i]);
        return v;
    }

    void propertyChanged(ItemDef& d, int) const override
    {
        QStringList errs;
        const QStringList v = vars(d);
        for (int i = 0; i < count(d); ++i) {
            const QString n = d.s(name_[i]);
            if (n.isEmpty() || !(n[0].isLetter() || n[0] == QLatin1Char('_')) ||
                std::any_of(n.begin(), n.end(), [](QChar c) { return !c.isLetterOrNumber() && c != QLatin1Char('_'); }))
                errs << QStringLiteral("name %1 is not a valid identifier").arg(i + 1);
            else if (v.indexOf(n) != i + 1)
                errs << QStringLiteral("name %1 is used twice").arg(i + 1);
            const QString e = checkExpr(QStringLiteral("d%1/dt").arg(n), d.s(rate_[i]), v);
            if (!e.isEmpty()) errs << e;
        }
        d.set(status_, errs.join(QLatin1Char('\n')));
    }

    QList<Channel> channels() const override
    {
        QList<Channel> c;
        for (int i = 0; i < kVars; ++i) c.push_back({i, QStringLiteral("Variable %1").arg(i + 1), unit::none});
        return c;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState* restore) const override;

    // Marker position for state y at time t (false when not plotted).
    bool markerAt(const ItemDef& d, Vec2 origin, double t, const double* y, Vec2& out) const
    {
        const int ax = int(d.i(px_)), ay = int(d.i(py_));
        if (ax == 0 || ay == 0) return false;
        auto pick = [&](int a) { return a == 1 ? t : y[a - 2]; };
        out = origin + Vec2{pick(ax), pick(ay)} * d.d(scale_);
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = d.c(color_);
        const Color edge = ctx.selected ? ctx.theme.highlight : c;
        const Vec2 o = ctx.pose.p;
        // Axes cross at the plot origin.
        const double a = ctx.px(14);
        ctx.batch.line(Layer::Back, o - Vec2{a, 0}, o + Vec2{a, 0}, 1.0f, edge.withAlpha(150));
        ctx.batch.line(Layer::Back, o - Vec2{0, a}, o + Vec2{0, a}, 1.0f, edge.withAlpha(150));
        double y[kVars];
        for (int i = 0; i < kVars; ++i) y[i] = ctx.snap ? double(ctx.snap->v[i]) : d.d(init_[i]);
        Vec2 m;
        if (markerAt(d, o, ctx.snap ? ctx.time : 0.0, y, m)) ctx.batch.disc(Layer::Body, m, ctx.px(5), c, edge, 1.5f);
        static const QString eq = QStringLiteral("ODE");
        ctx.batch.label(o, d.name.isEmpty() ? eq : d.name, ctx.theme.text, 0, 16);
        if (!d.s(status_).isEmpty()) {
            ctx.batch.label(o, d.s(status_), Color::fromRgb(0xda4453), 0, -18);
        } else if (ctx.snap && ctx.snap->v[6] > 0.5f) {
            static const QString bad = QStringLiteral("non-finite state: integration stopped");
            ctx.batch.label(o, bad, Color::fromRgb(0xda4453), 0, -18);
        }
    }

    int count_, name_[kVars], init_[kVars], rate_[kVars], status_, px_, py_, scale_, color_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class OdeRuntime final : public ItemRuntime {
public:
    OdeRuntime(const ItemDef& d, const OdeSystem& t, World& w, const RestoreState* restore)
        : ItemRuntime(d.id), t_(t), def_(std::make_shared<ItemDef>(d))
    {
        n_ = t.count(d);
        const QStringList v = t.vars(d);
        ok_ = true;
        for (int i = 0; i < n_; ++i) ok_ = ok_ && compileExpr(d.s(t.rate_[i]), v, w, rate_[i], nullptr);
        for (int i = 0; i < OdeSystem::kVars; ++i) y_[i] = i < n_ ? d.d(t.init_[i]) : 0.0;
        if (const auto* s = restore ? restore->customState(d.id) : nullptr) loadState(*s);
        origin_ = t.pose(d).p;
        Vec2 m;
        if (t.markerAt(d, origin_, w.time(), y_, m)) marker_.create(w, d.id, m, 0.0, 0.0, restore);
    }
    void destroy(World& w) override { marker_.destroy(w); }
    bool applyEdit(const ItemDef&, uint64_t, World&) override { return false; }

    void rates(const World& w, double t, const double* y, double* out) const
    {
        double vars[1 + OdeSystem::kVars];
        vars[0] = t;
        for (int i = 0; i < n_; ++i) vars[1 + i] = y[i];
        for (int i = 0; i < n_; ++i) out[i] = rate_[i].eval(vars, w.constantValues());
    }

    void preStep(World& w) override
    {
        const double h = w.dt(), t = w.time();
        if (ok_ && !bad_) {
            // Classic fourth-order Runge–Kutta over one physics step.
            double k1[4], k2[4], k3[4], k4[4], tmp[4];
            rates(w, t, y_, k1);
            for (int i = 0; i < n_; ++i) tmp[i] = y_[i] + 0.5 * h * k1[i];
            rates(w, t + 0.5 * h, tmp, k2);
            for (int i = 0; i < n_; ++i) tmp[i] = y_[i] + 0.5 * h * k2[i];
            rates(w, t + 0.5 * h, tmp, k3);
            for (int i = 0; i < n_; ++i) tmp[i] = y_[i] + h * k3[i];
            rates(w, t + h, tmp, k4);
            double next[4] = {0, 0, 0, 0};
            bool finite = true;
            for (int i = 0; i < n_; ++i) {
                next[i] = y_[i] + h / 6.0 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
                finite = finite && std::isfinite(next[i]);
            }
            if (finite)
                std::copy(next, next + n_, y_);
            else
                bad_ = true; // keep the last good state
        }
        if (!marker_.valid()) return;
        Vec2 m;
        if (t_.markerAt(*def_, origin_, t + h, y_, m))
            marker_.driveTo(w, m, 0.0);
        else
            marker_.hold(w);
    }

    bool snapshot(const World&, ItemSnap& s) const override
    {
        for (int i = 0; i < OdeSystem::kVars; ++i) s.v[i] = float(y_[i]);
        s.v[6] = bad_ || !ok_ ? 1.0f : 0.0f;
        return true;
    }
    void saveState(std::vector<double>& out) const override
    {
        out.assign(y_, y_ + OdeSystem::kVars);
        out.push_back(bad_ ? 1.0 : 0.0);
    }
    void loadState(const std::vector<double>& s) override
    {
        if (s.size() < OdeSystem::kVars + 1) return;
        std::copy(s.begin(), s.begin() + OdeSystem::kVars, y_);
        bad_ = s[OdeSystem::kVars] != 0.0;
    }

private:
    const OdeSystem& t_;
    ItemDefPtr def_;
    expr::Program rate_[OdeSystem::kVars];
    double y_[OdeSystem::kVars] = {0, 0, 0, 0};
    int n_ = 1;
    bool ok_ = false, bad_ = false;
    Vec2 origin_;
    MarkerBody marker_;
};
} // namespace

std::unique_ptr<ItemRuntime> OdeSystem::createRuntime(const ItemDef& d, World& w, const RestoreState* restore) const
{
    return std::make_unique<OdeRuntime>(d, *this, w, restore);
}

KINETIC_DEFINE_ITEM(OdeSystem)

} // namespace kn
