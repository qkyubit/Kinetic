// Scene-wide overlays computed from the document each frame: the field overlay
// (vector grid and field lines) and the optics ray tracer. Both reuse their
// buffers, so steady-state frames do not allocate.
#pragma once

#include "Document.h"

#include "core/Optics.h"
#include "core/RenderBatch.h"

namespace kn {

class FieldOverlay {
public:
    enum Mode : int { None, Gravity, Electric, Magnetic, Wind };
    void draw(RenderBatch& b, const ThemeColors& theme, const Document& doc, const SceneView& view, Mode mode,
              bool lines, Vec2 lo, Vec2 hi, double pxPerM);

private:
    Vec2 field(Vec2 p) const;
    void traceLine(RenderBatch& b, Vec2 start, double dir, Color c, Vec2 lo, Vec2 hi, double step);

    struct Source {
        const ItemDef* def;
        Pose pose;
    };
    std::vector<Source> sources_;
    std::vector<Vec2> sinks_; // points where field lines end (negative charges, S poles)
    std::vector<Vec2> line_;
    FieldKind kind_ = FieldKind::Gravity;
    Vec2 gravity_;
};

class OpticsView {
public:
    // Returns the number of traced segments.
    int draw(RenderBatch& b, const ThemeColors& theme, const Document& doc, const SceneView& view);

private:
    optics::Scene scene_;
    optics::Options options_;
    std::vector<optics::PathSegment> out_;
    std::vector<optics::Ray> stack_;
};

} // namespace kn
