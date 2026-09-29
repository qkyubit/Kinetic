// CPU-side batches filled by item types each frame and uploaded by the viewport
// as instanced (segments, discs/capsules) and batched (triangles) draws.
// Containers are cleared, never shrunk, so steady-state frames do not allocate.
#pragma once

#include "Item.h"
#include "Snapshot.h"

#include <QString>

#include <vector>

namespace kn {

enum class Layer : uint8_t { Back, Body, Front, Overlay, Count };
constexpr int kLayerCount = int(Layer::Count);

// Anti-aliased line segment, width in pixels.
struct SegInst {
    float x0, y0, x1, y1;
    float width;
    uint32_t color;
};

// Filled disc or capsule with outline. Capsule when (ax, ay) != 0: the spine runs
// from centre - axis to centre + axis.
struct DiscInst {
    float cx, cy, ax, ay;
    float radius, outlinePx, marker;
    uint32_t fill, outline;
};

struct TriVert {
    float x, y;
    uint32_t color;
};

// Text drawn by the QPainter overlay. `text` should be an existing (implicitly
// shared) string; numbers are kept as values and formatted by the overlay, so
// building labels in the render path does not allocate.
struct TextLabel {
    Vec2 pos;           // world position
    QString text;       // prefix
    double value = 0.0; // appended when hasValue
    QString unit;       // appended after the value
    bool hasValue = false;
    int decimals = 2;
    Color color;
    int dx = 0, dy = 0; // pixel offset
};

struct ThemeColors {
    Color background{255, 255, 255, 255};
    Color grid{0, 0, 0, 25};
    Color gridMajor{0, 0, 0, 50};
    Color axis{0, 0, 0, 90};
    Color text{0, 0, 0, 255};
    Color highlight{61, 174, 233, 255};
    Color outline{0, 0, 0, 160};
    Color dim{0, 0, 0, 100};
    bool dark = false;
};

class RenderBatch {
public:
    void clear();

    double pxPerM = 50.0;
    double px(double pixels) const { return pixels / pxPerM; }

    void line(Layer l, Vec2 a, Vec2 b, float widthPx, Color c);
    void polyline(Layer l, const Vec2* pts, int n, bool closed, float widthPx, Color c);
    void dashed(Layer l, Vec2 a, Vec2 b, float widthPx, Color c, double dashPx = 6.0);
    void arrow(Layer l, Vec2 from, Vec2 to, float widthPx, Color c, double headPx = 10.0);
    void disc(Layer l, Vec2 c, double r, Color fill, Color outline, float outlinePx = 1.5f, bool marker = false,
              double markerAngle = 0.0);
    void capsule(Layer l, Vec2 a, Vec2 b, double r, Color fill, Color outline, float outlinePx = 1.5f);
    void circleOutline(Layer l, Vec2 c, double r, Color col, float widthPx);
    void arc(Layer l, Vec2 c, double r, double a0, double a1, float widthPx, Color col, int segments = 32);
    void triangle(Layer l, Vec2 a, Vec2 b, Vec2 c, Color col);
    void convexFill(Layer l, const Vec2* pts, int n, Color col);
    void rectFill(Layer l, Vec2 lo, Vec2 hi, Color col);
    void ray(Vec2 a, Vec2 b, float widthPx, Color c); // additive blended layer
    void label(Vec2 pos, const QString& text, Color c, int dx = 0, int dy = 0);
    void valueLabel(Vec2 pos, const QString& prefix, double value, const QString& unit, int decimals, Color c,
                    int dx = 0, int dy = 0);
    // Draws a shape transformed by `pose`.
    void shape(Layer l, const Shape& s, const Pose& pose, Color fill, Color outline, float outlinePx, bool marker);

    std::vector<SegInst> segs[kLayerCount];
    std::vector<DiscInst> discs[kLayerCount];
    std::vector<TriVert> tris[kLayerCount];
    std::vector<SegInst> rays;
    std::vector<TextLabel> labels;
};

struct DrawContext {
    RenderBatch& batch;
    const ThemeColors& theme;
    const SceneView& view;
    double pxPerM = 50.0;
    bool selected = false;
    bool simActive = false;
    double time = 0.0;
    Pose pose;                       // resolved pose (live or document)
    const BodySnap* body = nullptr;  // live body state, if simulating
    const ItemSnap* snap = nullptr;  // live item state, if any
    double px(double pixels) const { return pixels / pxPerM; }
};

} // namespace kn
