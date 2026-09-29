// Trigger region: a Box2D sensor that logs when bodies enter and leave it.
// Enter/exit times appear in the Trigger Log panel; the count of bodies inside
// is a graphable channel.
#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

class TriggerZone final : public ItemType {
public:
    TriggerZone()
    {
        meta_ = {QStringLiteral("tool.trigger"), QStringLiteral("Trigger zone"), QStringLiteral("Analysis"),
                 QStringLiteral("chronometer"), QStringLiteral(":/icons/tool.svg"),
                 {QStringLiteral("sensor"), QStringLiteral("timer"), QStringLiteral("gate"), QStringLiteral("detector"),
                  QStringLiteral("photogate")},
                 QStringLiteral("Logs the times bodies enter and leave the region."), ItemKind::Tool};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Transform"));
        s.vec(QStringLiteral("position"), QStringLiteral("Position"), unit::m, -10000, 10000, {0, 0}).rebuild();
        s.angle(QStringLiteral("angle"), QStringLiteral("Angle"), -180, 180, 0).rebuild();
        s.beginGroup(QStringLiteral("Region"));
        w_ = s.size();
        s.num(QStringLiteral("width"), QStringLiteral("Width"), unit::m, 0.01, 1000, 1, 3).log().rebuild();
        h_ = s.size();
        s.num(QStringLiteral("height"), QStringLiteral("Height"), unit::m, 0.01, 1000, 1, 3).log().rebuild();
        s.beginGroup(QStringLiteral("Appearance"));
        color_ = s.size();
        s.colour(QStringLiteral("color"), QStringLiteral("Colour"), Color::fromRgb(0xf1c40f, 60));
        label_ = s.size();
        s.text(QStringLiteral("label"), QStringLiteral("Label"), QString());
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

    void shapes(const ItemDef& d, ShapeList& out) const override
    {
        out.clear();
        out.push_back(Shape::box(0.5 * d.d(w_), 0.5 * d.d(h_)));
    }

    bool scale(ItemDef& d, double sx, double sy) const override
    {
        d.set(w_, clampValue(schema_[w_], d.d(w_) * std::abs(sx)));
        d.set(h_, clampValue(schema_[h_], d.d(h_) * std::abs(sy)));
        return true;
    }

    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Bodies inside"), unit::none}, {5, QStringLiteral("Entries so far"), unit::none}};
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        Vec2 c[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        for (Vec2& p : c) p = ctx.pose.apply(p);
        const Color fill = d.c(color_);
        const Color edge = ctx.selected ? ctx.theme.highlight : fill.withAlpha(220);
        ctx.batch.convexFill(Layer::Back, c, 4, fill);
        for (int i = 0; i < 4; ++i) ctx.batch.dashed(Layer::Front, c[i], c[(i + 1) % 4], ctx.selected ? 2.0f : 1.5f, edge);
        static const QString inside = QStringLiteral(" · inside: ");
        const QString& text = d.s(label_).isEmpty() ? d.name : d.s(label_);
        if (ctx.snap) {
            ctx.batch.valueLabel(ctx.pose.apply({0, hh}), text + inside, ctx.snap->v[4], QString(), 0, ctx.theme.text, 0, -10);
        } else {
            ctx.batch.label(ctx.pose.apply({0, hh}), text, ctx.theme.text, 0, -10);
        }
    }

    int w_, h_, color_, label_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class TriggerRuntime final : public ItemRuntime {
public:
    TriggerRuntime(const ItemDef& d, const TriggerZone& t, World& w) : ItemRuntime(d.id)
    {
        const Pose p = t.pose(d);
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_staticBody;
        bd.position = {float(p.p.x), float(p.p.y)};
        bd.rotation = b2MakeRot(float(p.angle()));
        body_ = b2CreateBody(w.id(), &bd);
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.isSensor = true;
        sd.enableSensorEvents = true;
        sd.userData = static_cast<ItemRuntime*>(this);
        const b2Polygon box = b2MakeBox(float(0.5 * d.d(t.w_)), float(0.5 * d.d(t.h_)));
        b2CreatePolygonShape(body_, &sd, &box);
        inside_.reserve(64);
    }

    void destroy(World&) override
    {
        if (b2Body_IsValid(body_)) b2DestroyBody(body_);
    }

    // Colour and label edits are live; moving or resizing rebuilds the sensor
    // (World keeps the inside set through saveState/loadState).
    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const auto& t = static_cast<const TriggerZone&>(*d.type);
        const uint64_t geometry = propBit(t.positionIndex()) | propBit(t.angleIndex()) | propBit(t.w_) | propBit(t.h_);
        return (changed & (geometry | (1ull << 63))) == 0;
    }

    void sensorEvent(World& w, ItemId body, bool begin) override
    {
        if (!body) return;
        const auto it = std::find(inside_.begin(), inside_.end(), body);
        // A rebuilt world reports existing overlaps again: ignore repeats.
        if (begin == (it != inside_.end())) return;
        if (begin) {
            inside_.push_back(body);
            ++entries_;
        } else {
            inside_.erase(it);
        }
        TriggerEvent e;
        e.time = w.time() + w.dt(); // the event happened during the step being finished
        e.step = w.stepIndex() + 1;
        e.trigger = id();
        e.body = body;
        e.enter = begin;
        w.emitEvent(e);
    }

    void saveState(std::vector<double>& out) const override
    {
        out.clear();
        out.push_back(double(entries_));
        for (ItemId b : inside_) out.push_back(double(b));
    }
    void loadState(const std::vector<double>& s) override
    {
        if (s.empty()) return;
        entries_ = uint64_t(s[0]);
        inside_.clear();
        for (size_t i = 1; i < s.size(); ++i) inside_.push_back(ItemId(s[i]));
    }

    bool snapshot(const World&, ItemSnap& s) const override
    {
        s.v[4] = float(inside_.size());
        s.v[5] = float(entries_);
        return true;
    }

private:
    b2BodyId body_ = b2_nullBodyId;
    std::vector<ItemId> inside_;
    uint64_t entries_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> TriggerZone::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<TriggerRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(TriggerZone)

} // namespace kn
