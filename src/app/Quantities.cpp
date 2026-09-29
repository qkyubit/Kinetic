#include "Quantities.h"

namespace kn {

namespace {
struct BodyInfo {
    const char* name;
    const char* unit;
};
const BodyInfo kBody[] = {
    {"x (centre of mass)", "m"}, {"y (centre of mass)", "m"}, {"angle", "°"}, {"velocity x", "m/s"},
    {"velocity y", "m/s"},       {"speed", "m/s"},            {"angular velocity", "rad/s"},
    {"acceleration x", "m/s²"},  {"acceleration y", "m/s²"},  {"acceleration", "m/s²"},
    {"angular acceleration", "rad/s²"}, {"kinetic energy", "J"}, {"gravitational PE", "J"},
    {"momentum x", "kg·m/s"},    {"momentum y", "kg·m/s"},    {"momentum", "kg·m/s"},
    {"angular momentum (about origin)", "kg·m²/s"}, {"net force x", "N"}, {"net force y", "N"},
    {"net force", "N"},          {"force", "N"},
};
const BodyInfo kEnergy[] = {{"kinetic energy", "J"},   {"gravitational PE", "J"}, {"spring PE", "J"},
                            {"field PE", "J"},         {"total energy", "J"},     {"energy drift", "%"}};

Vec2 gravityOf(const Document& doc)
{
    const int gi = doc.world().type->schema().indexOf(QStringLiteral("gravity"));
    return gi >= 0 ? doc.world().v(gi) : Vec2{0, -9.81};
}
} // namespace

QString Quantity::name(const Document& doc) const
{
    switch (kind) {
    case Kind::Time: return QObject::tr("time");
    case Kind::Energy: return QObject::tr(kEnergy[code].name);
    case Kind::Body:
        if (code == SourceForce) return QObject::tr("%1 force").arg(QString::fromLatin1(forceSourceName(ForceSource(arg))).toLower());
        return QObject::tr(kBody[code].name);
    case Kind::Channel: {
        const ItemDef* d = doc.item(item);
        if (!d) return {};
        for (const auto& c : d->type->channels())
            if (c.index == code) return c.name.toLower();
        return {};
    }
    case Kind::Property: {
        const ItemDef* d = doc.item(item);
        return d && code < d->type->schema().size() ? d->type->schema()[code].label.toLower() : QString();
    }
    }
    return {};
}

QString Quantity::label(const Document& doc) const
{
    if (kind == Kind::Time) return QObject::tr("Time");
    if (kind == Kind::Energy) return QObject::tr("Scene · %1").arg(name(doc));
    const ItemDef* d = doc.item(item);
    return QStringLiteral("%1 · %2").arg(d ? d->name : QObject::tr("(deleted)"), name(doc));
}

QString Quantity::unit(const Document& doc) const
{
    switch (kind) {
    case Kind::Time: return QStringLiteral("s");
    case Kind::Energy: return QString::fromUtf8(kEnergy[code].unit);
    case Kind::Body: return QString::fromUtf8(kBody[code].unit);
    case Kind::Channel: {
        const ItemDef* d = doc.item(item);
        if (d)
            for (const auto& c : d->type->channels())
                if (c.index == code) return c.unit;
        return {};
    }
    case Kind::Property: {
        const ItemDef* d = doc.item(item);
        return d && code < d->type->schema().size() ? d->type->schema()[code].unit : QString();
    }
    }
    return {};
}

bool Quantity::eval(const SimController& sim, const Document& doc, double& out) const
{
    const Snapshot& s = sim.snapshot();
    switch (kind) {
    case Kind::Time:
        out = sim.live() ? s.time : 0.0;
        return true;
    case Kind::Energy: {
        if (!sim.live()) return false;
        const EnergySnap& e = s.energy;
        const double v[] = {e.kinetic, e.gravity, e.spring, e.field, e.total, e.driftPercent()};
        out = v[code];
        return true;
    }
    case Kind::Channel: {
        const ItemSnap* is = sim.itemSnap(item);
        const ItemDef* d = doc.item(item);
        if (!is || !d || code < 0 || code >= 8) return false;
        double scale = 1.0;
        for (const auto& c : d->type->channels())
            if (c.index == code) scale = c.scale;
        out = double(is->v[code]) * scale;
        return true;
    }
    case Kind::Property: {
        const ItemDef* d = doc.item(item);
        if (!d || code < 0 || code >= d->type->schema().size()) return false;
        out = numericValue(d->props[size_t(code)]) * d->type->schema()[code].scale;
        return true;
    }
    case Kind::Body: {
        const BodySnap* b = sim.bodySnap(item);
        if (!b) return false;
        const double m = b->mass, I = b->inertia;
        switch (code) {
        case X: out = b->cx; break;
        case Y: out = b->cy; break;
        case Angle: out = std::atan2(double(b->s), double(b->c)) * kDegPerRad; break;
        case Vx: out = b->vx; break;
        case Vy: out = b->vy; break;
        case Speed: out = std::hypot(b->vx, b->vy); break;
        case Omega: out = b->w; break;
        case Ax: out = b->ax; break;
        case Ay: out = b->ay; break;
        case Accel: out = std::hypot(b->ax, b->ay); break;
        case Alpha: out = b->alpha; break;
        case KE: out = 0.5 * m * (double(b->vx) * b->vx + double(b->vy) * b->vy) + 0.5 * I * double(b->w) * b->w; break;
        case PEg: {
            const Vec2 g = gravityOf(doc);
            const ItemDef* d = doc.item(item);
            const int gs = d ? d->type->schema().indexOf(QStringLiteral("gravityScale")) : -1;
            const double scale = gs >= 0 ? d->d(gs) : 1.0;
            out = -m * scale * (g.x * b->cx + g.y * b->cy);
            break;
        }
        case Px: out = m * b->vx; break;
        case Py: out = m * b->vy; break;
        case P: out = m * std::hypot(b->vx, b->vy); break;
        case L: out = I * b->w + m * (double(b->cx) * b->vy - double(b->cy) * b->vx); break;
        case Fx: out = b->fx; break;
        case Fy: out = b->fy; break;
        case F: out = std::hypot(b->fx, b->fy); break;
        case SourceForce:
            out = 0.0;
            for (int i = 0; i < b->forceCount; ++i)
                if (b->forces[i].source == uint8_t(arg)) out = std::hypot(b->forces[i].fx, b->forces[i].fy);
            break;
        default: return false;
        }
        return true;
    }
    }
    return false;
}

QList<Quantity> Quantity::options(const Document& doc, ItemId item)
{
    QList<Quantity> out;
    if (item == 0) {
        out << Quantity{Kind::Time, 0, 0, 0};
        for (int e = Kinetic; e <= Drift; ++e) out << Quantity{Kind::Energy, 0, e, 0};
        return out;
    }
    const ItemDef* d = doc.item(item);
    if (!d) return out;
    if (d->type->kind() == ItemKind::Body) {
        for (int c = X; c < SourceForce; ++c) out << Quantity{Kind::Body, item, c, 0};
        for (int s = 0; s < kForceSourceCount; ++s) out << Quantity{Kind::Body, item, SourceForce, s};
    }
    for (const auto& c : d->type->channels()) out << Quantity{Kind::Channel, item, c.index, 0};
    const auto& defs = d->type->schema().defs();
    for (size_t i = 0; i < defs.size(); ++i)
        if ((defs[i].type == PropType::Double || defs[i].type == PropType::Int) && !defs[i].has(PF_Hidden))
            out << Quantity{Kind::Property, item, int(i), 0};
    return out;
}

} // namespace kn
