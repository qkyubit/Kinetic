// 2D geometric optics: sequential-free ray tracing against mirrors, ideal thin
// lenses, refracting elements (spherical/flat boundaries of a dispersive glass)
// and absorbers, with Snell refraction, total internal reflection and optional
// Fresnel splitting. Runs on the UI thread from the document; reuses its
// buffers, so steady-state frames do not allocate.
#pragma once

#include "Math.h"

#include <QStringList>

#include <vector>

namespace kn::optics {

// Refractive index model; wavelengths in nm (formulas use µm).
struct Glass {
    enum Kind : uint8_t { Sellmeier, Cauchy };
    Kind kind = Cauchy;
    double b[3] = {0, 0, 0}, c[3] = {0, 0, 0}; // Sellmeier: n² = 1 + Σ b λ² / (λ² − c)
    double A = 1.5, B = 0.0;                   // Cauchy: n = A + B / λ²
    double index(double nm) const;
};

// Presets (index 0 is "Custom", a Cauchy glass).
int glassCount();
const QStringList& glassNames();
Glass glassPreset(int i);

enum class SurfaceKind : uint8_t { Mirror, Refract, Absorb, ThinLens };

struct Surface {
    SurfaceKind kind = SurfaceKind::Absorb;
    bool arc = false;
    Vec2 a, b;             // segment end points (arcs: unused)
    Vec2 center;           // arc
    double radius = 0.0;   // arc
    double a0 = 0.0, span = 0.0; // arc: counter-clockwise from a0 by span radians
    int element = -1;      // Refract: the element whose boundary this is
    double focal = 0.0;    // ThinLens (positive: converging)
    double reflectance = 1.0; // Mirror
};

struct Ray {
    Vec2 o, d;              // origin, unit direction
    float nm = 550.0f;      // wavelength
    float intensity = 1.0f;
    float rgb[3] = {1, 1, 1}; // display colour (sRGB-encoded, additive weights)
    int element = -1;       // element the ray travels inside (-1: air)
    int depth = 0;
    int from = -1;          // surface the ray left (skipped for the first hit)
};

struct Scene {
    std::vector<Surface> surfaces;
    std::vector<Glass> elements;
    std::vector<Ray> rays;
    void clear();
    int addElement(const Glass& g);
    void addSegment(SurfaceKind k, Vec2 a, Vec2 b, int element = -1);
    // Arc through angles [a0, a0 + span] (span > 0).
    void addArc(SurfaceKind k, Vec2 center, double r, double a0, double span, int element = -1);
    // Monochromatic ray.
    void emitRay(Vec2 o, Vec2 d, double nm, double intensity);
    // White light: `samples` rays spread over 400–700 nm whose colours add to white.
    void emitWhite(Vec2 o, Vec2 d, double intensity, int samples);
};

struct Options {
    int maxDepth = 64;          // interactions per path
    float minIntensity = 0.01f; // weaker branches are dropped
    double maxLength = 2000.0;  // length of rays leaving the scene, m
    int maxSegments = 60000;    // total output budget
    bool fresnel = true;        // split partial reflections at refracting boundaries
};

struct PathSegment {
    Vec2 a, b;
    float nm;
    float intensity;
    float rgb[3];
    int element; // medium the segment travels in
};

// Traces every ray in the scene. `out` and `stack` are cleared, not shrunk.
void trace(const Scene& scene, const Options& opt, std::vector<PathSegment>& out, std::vector<Ray>& stack);

// sRGB-encoded colour of a spectral line, normalised to a maximum channel of 1.
void wavelengthToRgb(double nm, float rgb[3]);

} // namespace kn::optics
