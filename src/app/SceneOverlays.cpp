#include "SceneOverlays.h"

#include "items/BodyItem.h"

#include "core/Forces.h"

namespace kn {

// --- fields ---------------------------------------------------------------------------------------

Vec2 FieldOverlay::field(Vec2 p) const
{
    Vec2 v;
    int replaced = 0;
    for (const Source& s : sources_)
        if (s.def->type->addField(*s.def, s.pose, kind_, p, v)) ++replaced;
    if (kind_ == FieldKind::Gravity) v += gravity_ * double(1 - replaced);
    return v;
}

void FieldOverlay::traceLine(RenderBatch& b, Vec2 start, double dir, Color c, Vec2 lo, Vec2 hi, double step)
{
    // Midpoint integration along the normalised field until it leaves the view,
    // reaches a sink or stalls.
    line_.clear();
    Vec2 p = start;
    line_.push_back(p);
    for (int i = 0; i < 600; ++i) {
        const Vec2 f1 = field(p);
        const double m1 = length(f1);
        if (m1 < 1e-30) break;
        const Vec2 mid = p + f1 * (dir * 0.5 * step / m1);
        const Vec2 f2 = field(mid);
        const double m2 = length(f2);
        if (m2 < 1e-30) break;
        p = p + f2 * (dir * step / m2);
        line_.push_back(p);
        if (p.x < lo.x || p.y < lo.y || p.x > hi.x || p.y > hi.y) break;
        bool sunk = false;
        for (const Vec2& s : sinks_) sunk = sunk || distance(s, p) < 1.5 * step;
        if (sunk) break;
    }
    if (line_.size() >= 2) b.polyline(Layer::Back, line_.data(), int(line_.size()), false, 1.2f, c);
}

void FieldOverlay::draw(RenderBatch& b, const ThemeColors& theme, const Document& doc, const SceneView& view, Mode mode,
                        bool lines, Vec2 lo, Vec2 hi, double pxPerM)
{
    if (mode == None) return;
    kind_ = FieldKind(int(mode) - 1);
    gravity_ = {};
    if (kind_ == FieldKind::Gravity) {
        const ItemDef& w = doc.world();
        gravity_ = w.v(w.type->schema().indexOf(QStringLiteral("gravity")));
    }
    // Only items that can contribute: field items, charged bodies, magnets.
    sources_.clear();
    sinks_.clear();
    for (const ItemDef& d : doc.scene().items) {
        const ItemKind k = d.type->kind();
        bool use = k == ItemKind::Field;
        double charge = 0.0;
        if (k == ItemKind::Body) {
            const auto* bt = static_cast<const BodyItemType*>(d.type);
            charge = d.d(bt->idx().charge);
            use = (kind_ == FieldKind::Electric && charge != 0.0) ||
                  (kind_ == FieldKind::Magnetic && d.type->typeId() == QLatin1String("body.magnet"));
        }
        if (!use || !doc.effectiveVisible(d)) continue;
        Pose pose = d.type->pose(d);
        view.pose(d.id, pose);
        sources_.push_back({&d, pose});
        if (kind_ == FieldKind::Electric && charge < 0) sinks_.push_back(pose.p);
    }
    if (sources_.empty() && lengthSq(gravity_) == 0.0) return;

    const Color col = forceSourceColor(kind_ == FieldKind::Gravity    ? ForceSource::Field
                                       : kind_ == FieldKind::Electric ? ForceSource::Electric
                                       : kind_ == FieldKind::Magnetic ? ForceSource::Magnetic
                                                                      : ForceSource::Wind);
    // Vector grid: fixed-length arrows, opacity by log-magnitude relative to the strongest.
    const double spacing = 42.0 / pxPerM;
    const long nx = long((hi.x - lo.x) / spacing) + 1, ny = long((hi.y - lo.y) / spacing) + 1;
    if (nx * ny > 6000) return;
    double maxLog = -1e300, minLog = 1e300;
    const double x0 = std::floor(lo.x / spacing) * spacing + spacing / 2, y0 = std::floor(lo.y / spacing) * spacing + spacing / 2;
    for (int pass = 0; pass < 2; ++pass)
        for (long j = 0; j <= ny; ++j)
            for (long i = 0; i <= nx; ++i) {
                const Vec2 p{x0 + i * spacing, y0 + j * spacing};
                const Vec2 f = field(p);
                const double m = length(f);
                if (m <= 0) continue;
                const double lg = std::log10(m);
                if (pass == 0) {
                    maxLog = std::max(maxLog, lg);
                    minLog = std::min(minLog, lg);
                    continue;
                }
                // Three decades of dynamic range.
                const double t = maxLog - minLog < 1e-9 ? 1.0 : std::clamp((lg - (maxLog - 3.0)) / 3.0, 0.0, 1.0);
                const Vec2 u = f / m;
                const double len = spacing * 0.7;
                b.arrow(Layer::Back, p - u * (len / 2), p + u * (len / 2), 1.3f, col.withAlpha(uint8_t(40 + 200 * t)), 6);
            }

    if (!lines || (kind_ != FieldKind::Electric && kind_ != FieldKind::Magnetic)) return;
    const double step = 5.0 / pxPerM;
    const Color lc = theme.dark ? col.withAlpha(170) : Color{uint8_t(col.r * 0.7), uint8_t(col.g * 0.7), uint8_t(col.b * 0.7), 190};
    const Vec2 margin{(hi.x - lo.x) * 0.25, (hi.y - lo.y) * 0.25};
    for (const Source& s : sources_) {
        if (s.def->type->kind() != ItemKind::Body) continue;
        if (kind_ == FieldKind::Electric) {
            const auto* bt = static_cast<const BodyItemType*>(s.def->type);
            const double q = s.def->d(bt->idx().charge);
            const bool hasPositive = std::any_of(sources_.begin(), sources_.end(), [](const Source& o) {
                return o.def->type->kind() == ItemKind::Body &&
                       o.def->d(static_cast<const BodyItemType*>(o.def->type)->idx().charge) > 0;
            });
            // Lines start at positive charges (or at negative ones, backwards, when there are none).
            if (q < 0 && hasPositive) continue;
            const int n = 16;
            for (int k = 0; k < n; ++k) {
                const double a = 2 * kPi * (k + 0.5) / n;
                traceLine(b, s.pose.p + Vec2{std::cos(a), std::sin(a)} * (8.0 / pxPerM), q > 0 ? 1.0 : -1.0, lc,
                          lo - margin, hi + margin, step);
            }
        } else {
            // Magnets: lines leave around the north pole.
            const int wi = s.def->type->schema().indexOf(QStringLiteral("width"));
            const int mi = s.def->type->schema().indexOf(QStringLiteral("moment"));
            const double hw = 0.5 * s.def->d(wi), sign = s.def->d(mi) >= 0 ? 1.0 : -1.0;
            const Vec2 north = s.pose.apply({sign * hw, 0});
            const int n = 14;
            for (int k = 0; k < n; ++k) {
                const double a = s.pose.angle() + (sign > 0 ? 0.0 : kPi) + kPi * ((k + 0.5) / n - 0.5) * 1.8;
                traceLine(b, north + Vec2{std::cos(a), std::sin(a)} * (0.25 * hw), 1.0, lc, lo - margin, hi + margin, step);
            }
        }
    }
}

// --- optics ------------------------------------------------------------------------------------------

int OpticsView::draw(RenderBatch& b, const ThemeColors& theme, const Document& doc, const SceneView& view)
{
    scene_.clear();
    bool any = false;
    for (const ItemDef& d : doc.scene().items) {
        if (d.type->kind() != ItemKind::Optics || !doc.effectiveVisible(d)) continue;
        Pose pose = d.type->pose(d);
        view.pose(d.id, pose);
        d.type->addOptics(d, pose, scene_);
        any = true;
    }
    if (!any || scene_.rays.empty()) return 0;
    optics::trace(scene_, options_, out_, stack_);
    for (const optics::PathSegment& s : out_) {
        if (theme.dark) {
            // Additive: white samples add back up to white where they overlap.
            b.ray(s.a, s.b, 1.6f,
                  Color{uint8_t(255 * s.rgb[0]), uint8_t(255 * s.rgb[1]), uint8_t(255 * s.rgb[2]),
                        uint8_t(std::clamp(s.intensity, 0.0f, 1.0f) * 255)});
        } else {
            // Light canvases cannot show additive light: draw each ray in its hue.
            float rgb[3];
            optics::wavelengthToRgb(s.nm, rgb);
            const float w = std::max({s.rgb[0], s.rgb[1], s.rgb[2]});
            const float a = std::clamp(s.intensity * (w >= 0.99f ? 1.0f : 0.55f), 0.0f, 1.0f);
            b.line(Layer::Front, s.a, s.b, 1.6f,
                   Color{uint8_t(200 * rgb[0]), uint8_t(200 * rgb[1]), uint8_t(200 * rgb[2]), uint8_t(a * 255)});
        }
    }
    return int(out_.size());
}

} // namespace kn
