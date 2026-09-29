#include "Analysis.h"

#include <QPainter>
#include <QSettings>

namespace kn {

namespace {
constexpr size_t kTrailCapacity = 2048; // points per trail; sampling adapts to the trail length

int propIndex(const ItemDef& d, const char* key) { return d.type->schema().indexOf(QLatin1String(key)); }

QString si(double v, const QString& unit, int decimals = 2)
{
    const double a = std::abs(v);
    if (a >= 1e6) return QStringLiteral("%1 M%2").arg(v / 1e6, 0, 'f', decimals).arg(unit);
    if (a >= 1e3) return QStringLiteral("%1 k%2").arg(v / 1e3, 0, 'f', decimals).arg(unit);
    return QStringLiteral("%1 %2").arg(v, 0, 'f', decimals).arg(unit);
}
} // namespace

void AnalysisOptions::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("analysis"));
    forces = ForceMode(s.value(QStringLiteral("forces"), int(forces)).toInt());
    components = s.value(QStringLiteral("components"), components).toBool();
    net = s.value(QStringLiteral("net"), net).toBool();
    velocity = s.value(QStringLiteral("velocity"), velocity).toBool();
    acceleration = s.value(QStringLiteral("acceleration"), acceleration).toBool();
    labels = s.value(QStringLiteral("labels"), labels).toBool();
    scaleMode = ScaleMode(s.value(QStringLiteral("scaleMode"), int(scaleMode)).toInt());
    weightPx = s.value(QStringLiteral("weightPx"), weightPx).toDouble();
    pxPerNewton = s.value(QStringLiteral("pxPerNewton"), pxPerNewton).toDouble();
    pxPerMps = s.value(QStringLiteral("pxPerMps"), pxPerMps).toDouble();
    pxPerMps2 = s.value(QStringLiteral("pxPerMps2"), pxPerMps2).toDouble();
    trails = s.value(QStringLiteral("trails"), trails).toBool();
    energy = s.value(QStringLiteral("energy"), energy).toBool();
    field = std::clamp(s.value(QStringLiteral("field"), field).toInt(), 0, 4);
    fieldLines = s.value(QStringLiteral("fieldLines"), fieldLines).toBool();
}

void AnalysisOptions::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("analysis"));
    s.setValue(QStringLiteral("forces"), int(forces));
    s.setValue(QStringLiteral("components"), components);
    s.setValue(QStringLiteral("net"), net);
    s.setValue(QStringLiteral("velocity"), velocity);
    s.setValue(QStringLiteral("acceleration"), acceleration);
    s.setValue(QStringLiteral("labels"), labels);
    s.setValue(QStringLiteral("scaleMode"), int(scaleMode));
    s.setValue(QStringLiteral("weightPx"), weightPx);
    s.setValue(QStringLiteral("pxPerNewton"), pxPerNewton);
    s.setValue(QStringLiteral("pxPerMps"), pxPerMps);
    s.setValue(QStringLiteral("pxPerMps2"), pxPerMps2);
    s.setValue(QStringLiteral("trails"), trails);
    s.setValue(QStringLiteral("energy"), energy);
    s.setValue(QStringLiteral("field"), field);
    s.setValue(QStringLiteral("fieldLines"), fieldLines);
}

Analysis::Analysis(Document* doc, SimController* sim, QObject* parent) : QObject(parent), doc_(doc), sim_(sim)
{
    opts_.load();
    connect(sim_, &SimController::snapshotUpdated, this, &Analysis::sample);
    connect(doc_, &Document::sceneReset, this, [this] {
        clearTrails();
        refreshTrailSet();
    });
    connect(doc_, &Document::itemAdded, this, &Analysis::refreshTrailSet);
    connect(doc_, &Document::itemRemoved, this, &Analysis::refreshTrailSet);
    connect(doc_, &Document::itemChanged, this, &Analysis::refreshTrailSet);
    refreshTrailSet();
}

void Analysis::setOptions(const AnalysisOptions& o)
{
    opts_ = o;
    opts_.save();
    emit changed();
}

void Analysis::clearTrails()
{
    for (auto& [id, t] : trails_) t.count = t.head = 0;
    emit changed();
}

void Analysis::refreshTrailSet()
{
    trailIds_.clear();
    forceIds_.clear();
    for (const ItemDef& d : doc_->scene().items) {
        if (d.type->kind() != ItemKind::Body) continue;
        const int fi = propIndex(d, "showForces");
        if (fi >= 0 && d.b(fi) && doc_->effectiveVisible(d)) forceIds_.push_back(d.id);
        const int ti = propIndex(d, "trail");
        if (ti < 0 || !d.b(ti)) continue;
        trailIds_.push_back(d.id);
        Trail& t = trails_[d.id]; // allocation only when a trail is first enabled
        if (t.ring.size() != kTrailCapacity) t.ring.resize(kTrailCapacity);
        const int li = propIndex(d, "trailLength");
        t.length = li >= 0 ? d.d(li) : 5.0;
    }
}

void Analysis::sample()
{
    if (!sim_->live()) return;
    const Snapshot& s = sim_->snapshot();
    if (s.generation != generation_ || s.time < lastTime_ - 1e-9) {
        // New run or scrubbed back: trails restart.
        generation_ = s.generation;
        for (auto& [id, t] : trails_) t.count = t.head = 0;
    }
    lastTime_ = s.time;
    for (ItemId id : trailIds_) {
        const BodySnap* b = sim_->bodySnap(id);
        auto it = trails_.find(id);
        if (!b || it == trails_.end()) continue;
        Trail& t = it->second;
        // Enough points for the whole trail length without exceeding the ring.
        const double minDt = t.length / double(kTrailCapacity - 2);
        if (t.count > 0) {
            const TrailPoint& last = t.ring[(t.head + kTrailCapacity - 1) % kTrailCapacity];
            if (s.time - last.t < minDt) continue;
        }
        t.ring[t.head] = {s.time, b->cx, b->cy};
        t.head = (t.head + 1) % kTrailCapacity;
        t.count = std::min(t.count + 1, kTrailCapacity);
    }
}

void Analysis::draw(RenderBatch& b, const ThemeColors& theme, double pxPerM, const std::unordered_set<ItemId>& selected)
{
    if (!sim_->live()) return;
    const double now = sim_->snapshot().time;
    if (opts_.trails) {
        for (ItemId id : trailIds_) {
            const auto it = trails_.find(id);
            const ItemDef* d = doc_->item(id);
            if (it == trails_.end() || !d || !doc_->effectiveVisible(*d)) continue;
            const Trail& t = it->second;
            const int ci = propIndex(*d, "trailColor"), fi = propIndex(*d, "trailFade");
            const Color c = ci >= 0 ? d->c(ci) : theme.highlight;
            const bool fade = fi >= 0 && d->b(fi);
            const BodySnap* body = sim_->bodySnap(id);
            // Oldest to newest, then to the body's current position.
            Vec2 prev;
            bool havePrev = false;
            for (size_t k = 0; k < t.count; ++k) {
                const TrailPoint& p = t.ring[(t.head + kTrailCapacity - t.count + k) % kTrailCapacity];
                const double age = now - p.t;
                if (age > t.length) continue;
                const Vec2 cur{p.x, p.y};
                if (havePrev) {
                    const double a = fade ? std::clamp(1.0 - age / t.length, 0.0, 1.0) : 1.0;
                    b.line(Layer::Back, prev, cur, 2.0f, c.withAlpha(uint8_t(c.a * a)));
                }
                prev = cur;
                havePrev = true;
            }
            if (havePrev && body) b.line(Layer::Back, prev, {body->cx, body->cy}, 2.0f, c);
        }
    }
    if (opts_.forces == AnalysisOptions::ForceMode::Off && !opts_.velocity && !opts_.acceleration) return;
    const bool forces = opts_.forces != AnalysisOptions::ForceMode::Off;
    if (opts_.forces == AnalysisOptions::ForceMode::All) {
        for (const BodySnap& body : sim_->snapshot().bodies)
            if (body.bodyType == 2) drawVectors(b, theme, pxPerM, body, true);
        return;
    }
    // Bodies flagged "Show force vectors", then the selection.
    for (ItemId id : forceIds_)
        if (const BodySnap* body = sim_->bodySnap(id); body && body->bodyType == 2)
            drawVectors(b, theme, pxPerM, *body, forces);
    for (ItemId id : selected) {
        if (std::find(forceIds_.begin(), forceIds_.end(), id) != forceIds_.end()) continue;
        if (const BodySnap* body = sim_->bodySnap(id); body && body->bodyType == 2)
            drawVectors(b, theme, pxPerM, *body, forces);
    }
}

void Analysis::drawVectors(RenderBatch& b, const ThemeColors& theme, double pxPerM, const BodySnap& body,
                           bool showForces)
{
    const Vec2 c{body.cx, body.cy};
    const double g = std::max(1e-6, length(doc_->world().v(doc_->world().type->schema().indexOf(QStringLiteral("gravity")))));
    auto forceLen = [&](double f) {
        const double px = opts_.scaleMode == AnalysisOptions::ScaleMode::RelativeToWeight
                              ? f / (std::max(double(body.mass), 1e-9) * g) * opts_.weightPx
                              : f * opts_.pxPerNewton;
        return px / pxPerM; // metres
    };
    static const QString nUnit = QStringLiteral(" N"), vUnit = QStringLiteral(" m/s"), aUnit = QStringLiteral(" m/s²");
    static const QString netLabel = QStringLiteral("F = "), vLabel = QStringLiteral("v = "), aLabel = QStringLiteral("a = ");
    static QString sourceLabels[kForceSourceCount];
    if (sourceLabels[0].isEmpty())
        for (int i = 0; i < kForceSourceCount; ++i)
            sourceLabels[i] = QString::fromLatin1(forceSourceName(ForceSource(i))) + QStringLiteral(" ");
    if (showForces && opts_.components) {
        for (int i = 0; i < body.forceCount; ++i) {
            const ForceEntry& f = body.forces[i];
            const double mag = std::hypot(f.fx, f.fy);
            if (mag < 1e-6) continue;
            const Vec2 dir{f.fx / mag, f.fy / mag};
            const Vec2 tip = c + dir * forceLen(mag);
            const Color col = forceSourceColor(ForceSource(f.source));
            b.arrow(Layer::Overlay, c, tip, 2.0f, col, 9);
            if (opts_.labels) {
                b.valueLabel(tip + dir * (8.0 / pxPerM), sourceLabels[f.source], mag, nUnit, 1, col);
                b.labels.back().si = true;
            }
        }
    }
    if (showForces && opts_.net) {
        const double mag = std::hypot(body.fx, body.fy);
        if (mag > 1e-6) {
            const Vec2 dir{body.fx / mag, body.fy / mag};
            const Vec2 tip = c + dir * forceLen(mag);
            b.arrow(Layer::Overlay, c, tip, 3.5f, theme.text, 12);
            if (opts_.labels) {
                b.valueLabel(tip + dir * (10.0 / pxPerM), netLabel, mag, nUnit, 1, theme.text);
                b.labels.back().si = true;
            }
        }
    }
    if (opts_.velocity) {
        const double v = std::hypot(body.vx, body.vy);
        if (v > 1e-6) {
            const Vec2 dir{body.vx / v, body.vy / v};
            const Vec2 tip = c + dir * (v * opts_.pxPerMps / pxPerM);
            const Color col = Color::fromRgb(0x27ae60);
            b.arrow(Layer::Overlay, c, tip, 2.5f, col, 10);
            if (opts_.labels) b.valueLabel(tip + dir * (8.0 / pxPerM), vLabel, v, vUnit, 2, col);
        }
    }
    if (opts_.acceleration) {
        const double a = std::hypot(body.ax, body.ay);
        if (a > 1e-6) {
            const Vec2 dir{body.ax / a, body.ay / a};
            const Vec2 tip = c + dir * (a * opts_.pxPerMps2 / pxPerM);
            const Color col = Color::fromRgb(0xe84393);
            b.arrow(Layer::Overlay, c, tip, 2.5f, col, 10);
            if (opts_.labels) b.valueLabel(tip + dir * (8.0 / pxPerM), aLabel, a, aUnit, 2, col);
        }
    }
}

void Analysis::paintOverlay(QPainter& p, const ThemeColors& theme, QPointF cursor, ItemId probe, QSize size) const
{
    if (!sim_->live()) return;
    const QFontMetricsF fm(p.font());
    const QColor text(theme.text.r, theme.text.g, theme.text.b);
    QColor bg(theme.background.r, theme.background.g, theme.background.b, 225);
    QColor frame = text;
    frame.setAlpha(60);
    auto box = [&](QPointF at, const QStringList& lines, bool alignRight) {
        double w = 0;
        for (const QString& l : lines) w = std::max(w, fm.horizontalAdvance(l));
        const QRectF r(alignRight ? at.x() - w - 16 : at.x(), at.y(), w + 16, fm.height() * lines.size() + 10);
        QRectF rr = r;
        if (rr.right() > size.width() - 4) rr.moveRight(size.width() - 4);
        if (rr.bottom() > size.height() - 4) rr.moveBottom(size.height() - 4);
        p.setPen(frame);
        p.setBrush(bg);
        p.drawRoundedRect(rr, 4, 4);
        p.setPen(text);
        double y = rr.top() + 5 + fm.ascent();
        for (const QString& l : lines) {
            p.drawText(QPointF(rr.left() + 8, y), l);
            y += fm.height();
        }
    };
    if (opts_.energy) {
        const EnergySnap& e = sim_->snapshot().energy;
        QStringList lines{tr("Kinetic  %1").arg(si(e.kinetic, QStringLiteral("J"))),
                          tr("Gravitational PE  %1").arg(si(e.gravity, QStringLiteral("J"))),
                          tr("Spring PE  %1").arg(si(e.spring, QStringLiteral("J")))};
        if (std::abs(e.field) > 0) lines << tr("Field PE  %1").arg(si(e.field, QStringLiteral("J")));
        lines << tr("Total  %1").arg(si(e.total, QStringLiteral("J")))
              << tr("Drift  %1 %").arg(e.driftPercent(), 0, 'f', 3);
        box(QPointF(10, 10), lines, false);
    }
    if (probe) {
        const BodySnap* b = sim_->bodySnap(probe);
        const ItemDef* d = doc_->item(probe);
        if (!b || !d) return;
        const double speed = std::hypot(b->vx, b->vy);
        QStringList lines{d->name,
                          tr("position  (%1, %2) m").arg(b->cx, 0, 'f', 3).arg(b->cy, 0, 'f', 3),
                          tr("velocity  (%1, %2) m/s,  |v| %3 m/s").arg(b->vx, 0, 'f', 3).arg(b->vy, 0, 'f', 3).arg(speed, 0, 'f', 3),
                          tr("angular velocity  %1 rad/s").arg(b->w, 0, 'f', 3),
                          tr("acceleration  (%1, %2) m/s²").arg(b->ax, 0, 'f', 3).arg(b->ay, 0, 'f', 3),
                          tr("net force  %1  (%2, %3)")
                              .arg(si(std::hypot(b->fx, b->fy), QStringLiteral("N")))
                              .arg(b->fx, 0, 'f', 1)
                              .arg(b->fy, 0, 'f', 1),
                          tr("kinetic energy  %1").arg(si(0.5 * b->mass * speed * speed + 0.5 * b->inertia * b->w * b->w, QStringLiteral("J"))),
                          tr("momentum  %1 kg·m/s").arg(b->mass * speed, 0, 'f', 3)};
        for (int i = 0; i < b->forceCount; ++i) {
            const ForceEntry& f = b->forces[i];
            lines << QStringLiteral("  %1  %2").arg(QString::fromLatin1(forceSourceName(ForceSource(f.source))),
                                                     si(std::hypot(f.fx, f.fy), QStringLiteral("N")));
        }
        if (!b->awake) lines << tr("(sleeping)");
        box(cursor + QPointF(18, 18), lines, false);
    }
}

} // namespace kn
