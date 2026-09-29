#include "EquationItem.h"

namespace kn {

bool compileExpr(const QString& src, const QStringList& vars, World& w, expr::Program& out, QString* error)
{
    expr::Symbols sym;
    sym.variables = vars;
    sym.constant = [&w](const QString& n) { return w.constantSlot(n); };
    expr::Error e;
    if (out.compile(src, sym, &e)) return true;
    if (error) *error = e.toString();
    return false;
}

QString checkExpr(const QString& label, const QString& src, const QStringList& vars)
{
    expr::Symbols sym;
    sym.variables = vars;
    sym.constant = [](const QString&) { return 0; }; // any other name may be a constant
    expr::Program p;
    expr::Error e;
    if (p.compile(src, sym, &e)) return QString();
    return QStringLiteral("%1: %2").arg(label, e.toString());
}

const expr::Program* cachedExpr(const QString& src, const QStringList& vars)
{
    struct Entry {
        QString src;
        QStringList vars;
        expr::Program prog;
        bool ok = false;
    };
    thread_local std::vector<Entry> cache;
    thread_local size_t nextSlot = 0;
    for (const Entry& e : cache)
        if (e.src == src && e.vars == vars) return e.ok ? &e.prog : nullptr;
    if (cache.size() < 64) cache.emplace_back();
    Entry& e = cache.size() < 64 ? cache.back() : cache[nextSlot++ % cache.size()];
    e.src = src;
    e.vars = vars;
    expr::Symbols sym;
    sym.variables = vars;
    sym.constant = [](const QString&) { return -1; }; // previews cannot see constants
    e.ok = e.prog.compile(src, sym, nullptr);
    return e.ok ? &e.prog : nullptr;
}

void addTrailProperties(PropertySchema& s, bool on, Color c)
{
    s.beginGroup(QStringLiteral("Analysis"));
    s.boolean(QStringLiteral("trail"), QStringLiteral("Show trail"), on);
    s.num(QStringLiteral("trailLength"), QStringLiteral("Trail length"), unit::s, 0.1, 60, 10, 1);
    s.colour(QStringLiteral("trailColor"), QStringLiteral("Trail colour"), c);
    s.boolean(QStringLiteral("trailFade"), QStringLiteral("Fade trail"), true);
}

void MarkerBody::create(World& w, ItemId id, Vec2 p, double angle, double radius, const RestoreState* restore)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_kinematicBody;
    bd.position = {float(p.x), float(p.y)};
    bd.rotation = b2MakeRot(float(angle));
    if (const BodyState* s = restore ? restore->body(id) : nullptr) {
        bd.position = {s->px, s->py};
        bd.rotation = {s->qc, s->qs};
        bd.linearVelocity = {s->vx, s->vy};
        bd.angularVelocity = s->w;
    }
    const b2BodyId b = b2CreateBody(w.id(), &bd);
    if (radius > 0) {
        b2ShapeDef sd = b2DefaultShapeDef();
        const b2Circle c{{0, 0}, float(radius)};
        b2CreateCircleShape(b, &sd, &c);
    }
    index_ = w.registerBody(id, b);
}

void MarkerBody::destroy(World& w)
{
    if (index_ < 0) return;
    const b2BodyId b = w.body(index_).id;
    w.unregisterBody(index_);
    if (b2Body_IsValid(b)) b2DestroyBody(b);
    index_ = -1;
}

void MarkerBody::driveTo(World& w, Vec2 p, double angle)
{
    const BodyRec& r = w.body(index_);
    const float h = w.dt();
    const b2Vec2 v{float((p.x - r.origin.x) / h), float((p.y - r.origin.y) / h)};
    const float da = float(wrapAngle(angle - std::atan2(double(r.rot.s), double(r.rot.c))));
    b2Body_SetLinearVelocity(r.id, v);
    b2Body_SetAngularVelocity(r.id, da / h);
}

void MarkerBody::hold(World& w)
{
    const BodyRec& r = w.body(index_);
    b2Body_SetLinearVelocity(r.id, {0, 0});
    b2Body_SetAngularVelocity(r.id, 0);
}

Vec2 MarkerBody::position(const World& w) const
{
    const BodyRec& r = w.body(index_);
    return {r.origin.x, r.origin.y};
}

double MarkerBody::angle(const World& w) const
{
    const BodyRec& r = w.body(index_);
    return std::atan2(double(r.rot.s), double(r.rot.c));
}

} // namespace kn
