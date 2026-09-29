// Helpers shared by equation-driven items: compiling expressions against the
// world's constants, validating them in the editor, and a kinematic "marker"
// body that equation items move (so trails, graphs and collisions work).
#pragma once

#include "core/Expr.h"
#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

// Compiles against the world's constant table (unknown names become constants
// that stay NaN until a Constant item defines them).
bool compileExpr(const QString& src, const QStringList& vars, World& w, expr::Program& out, QString* error);

// Syntax check for the editor. Returns "" or "<label>: column N: message".
QString checkExpr(const QString& label, const QString& src, const QStringList& vars);

// Editor-side compiled program cache (keyed by source and variables), so
// drawing previews does not recompile or allocate every frame.
const expr::Program* cachedExpr(const QString& src, const QStringList& vars);

// Adds the standard trail properties (read by the Analysis overlay).
void addTrailProperties(PropertySchema& s, bool on, Color c);

class MarkerBody {
public:
    // Kinematic body at `p`, with a circle of `radius` (0: no collision shape).
    void create(World& w, ItemId id, Vec2 p, double angle, double radius, const RestoreState* restore);
    void destroy(World& w);
    // Moves the body to `p`/`angle` over the coming step (kinematic velocity).
    void driveTo(World& w, Vec2 p, double angle);
    void hold(World& w);
    bool valid() const { return index_ >= 0; }
    Vec2 position(const World& w) const;
    double angle(const World& w) const;

private:
    int index_ = -1;
};

} // namespace kn
