#include "Optics.h"

#include <algorithm>
#include <cmath>

namespace kn::optics {

// --- glasses -------------------------------------------------------------------------------

double Glass::index(double nm) const
{
    const double l = nm * 1e-3, l2 = l * l;
    if (kind == Cauchy) return A + B / l2;
    double n2 = 1.0;
    for (int i = 0; i < 3; ++i) n2 += b[i] * l2 / (l2 - c[i]);
    return std::sqrt(std::max(n2, 1.0));
}

namespace {
Glass sellmeier(double b1, double b2, double b3, double c1, double c2, double c3)
{
    Glass g;
    g.kind = Glass::Sellmeier;
    g.b[0] = b1, g.b[1] = b2, g.b[2] = b3;
    g.c[0] = c1, g.c[1] = c2, g.c[2] = c3;
    return g;
}
Glass cauchy(double a, double b)
{
    Glass g;
    g.A = a;
    g.B = b;
    return g;
}
} // namespace

int glassCount() { return int(glassNames().size()); }

const QStringList& glassNames()
{
    static const QStringList names = {QStringLiteral("Custom (Cauchy)"), QStringLiteral("Crown glass (N-BK7)"),
                                      QStringLiteral("Flint glass (F2)"), QStringLiteral("Dense flint (SF11)"),
                                      QStringLiteral("Fused silica"),     QStringLiteral("Acrylic (PMMA)"),
                                      QStringLiteral("Water"),            QStringLiteral("Diamond")};
    return names;
}

Glass glassPreset(int i)
{
    // Sellmeier coefficients from the Schott catalogue / Malitson (silica) /
    // Peter (diamond); Cauchy fits for PMMA and water.
    switch (i) {
    case 1: return sellmeier(1.03961212, 0.231792344, 1.01046945, 0.00600069867, 0.0200179144, 103.560653);
    case 2: return sellmeier(1.34533359, 0.209073176, 0.937357162, 0.00997743871, 0.0470450767, 111.886764);
    case 3: return sellmeier(1.73759695, 0.313747346, 1.89878101, 0.013188707, 0.0623068142, 155.23629);
    case 4: return sellmeier(0.6961663, 0.4079426, 0.8974794, 0.0684043 * 0.0684043, 0.1162414 * 0.1162414, 9.896161 * 9.896161);
    case 5: return cauchy(1.4814, 0.00356);
    case 6: return cauchy(1.3242, 0.00306);
    case 7: return sellmeier(0.3306, 4.3356, 0.0, 0.1750 * 0.1750, 0.1060 * 0.1060, 0.0);
    default: return cauchy(1.5, 0.004);
    }
}

// --- colour ----------------------------------------------------------------------------------

namespace {
double lobe(double x, double mu, double s1, double s2)
{
    const double t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}
double encode(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
} // namespace

void wavelengthToRgb(double nm, float rgb[3])
{
    // CIE 1931 matching functions, multi-lobe fit (Wyman, Sloan & Shirley 2013).
    const double X = 1.056 * lobe(nm, 599.8, 37.9, 31.0) + 0.362 * lobe(nm, 442.0, 16.0, 26.7) -
                     0.065 * lobe(nm, 501.1, 20.4, 26.2);
    const double Y = 0.821 * lobe(nm, 568.8, 46.9, 40.5) + 0.286 * lobe(nm, 530.9, 16.3, 31.1);
    const double Z = 1.217 * lobe(nm, 437.0, 11.8, 36.0) + 0.681 * lobe(nm, 459.0, 26.0, 13.8);
    double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
    r = std::max(r, 0.0), g = std::max(g, 0.0), b = std::max(b, 0.0);
    const double m = std::max({r, g, b, 1e-9});
    rgb[0] = float(encode(r / m));
    rgb[1] = float(encode(g / m));
    rgb[2] = float(encode(b / m));
}

// --- scene -------------------------------------------------------------------------------------

void Scene::clear()
{
    surfaces.clear();
    elements.clear();
    rays.clear();
}

int Scene::addElement(const Glass& g)
{
    elements.push_back(g);
    return int(elements.size()) - 1;
}

void Scene::addSegment(SurfaceKind k, Vec2 a, Vec2 b, int element)
{
    Surface s;
    s.kind = k;
    s.a = a;
    s.b = b;
    s.element = element;
    surfaces.push_back(s);
}

void Scene::addArc(SurfaceKind k, Vec2 center, double r, double a0, double span, int element)
{
    Surface s;
    s.kind = k;
    s.arc = true;
    s.center = center;
    s.radius = r;
    s.a0 = a0;
    s.span = span;
    s.element = element;
    surfaces.push_back(s);
}

void Scene::emitRay(Vec2 o, Vec2 d, double nm, double intensity)
{
    Ray r;
    r.o = o;
    r.d = normalized(d);
    r.nm = float(nm);
    r.intensity = float(intensity);
    wavelengthToRgb(nm, r.rgb);
    rays.push_back(r);
}

void Scene::emitWhite(Vec2 o, Vec2 d, double intensity, int samples)
{
    samples = std::clamp(samples, 1, 64);
    // Per-channel weights so the samples' colours sum to white.
    float sum[3] = {0, 0, 0};
    float col[64][3];
    for (int i = 0; i < samples; ++i) {
        const double nm = samples == 1 ? 550.0 : 400.0 + 300.0 * (i + 0.5) / samples;
        wavelengthToRgb(nm, col[i]);
        for (int k = 0; k < 3; ++k) sum[k] += col[i][k];
    }
    for (int i = 0; i < samples; ++i) {
        Ray r;
        r.o = o;
        r.d = normalized(d);
        r.nm = float(samples == 1 ? 550.0 : 400.0 + 300.0 * (i + 0.5) / samples);
        r.intensity = float(intensity);
        for (int k = 0; k < 3; ++k) r.rgb[k] = sum[k] > 0 ? col[i][k] / sum[k] : 0.0f;
        rays.push_back(r);
    }
}

// --- tracing ----------------------------------------------------------------------------------

namespace {

struct Hit {
    double t = 0;
    Vec2 n; // unit normal (either orientation)
    int surface = -1;
};

constexpr double kEps = 1e-9;

bool hitSegment(const Surface& s, Vec2 o, Vec2 d, double& t, Vec2& n)
{
    const Vec2 e = s.b - s.a;
    const double den = cross(d, e);
    if (std::abs(den) < 1e-15) return false;
    const Vec2 w = s.a - o;
    const double tt = cross(w, e) / den;
    const double u = cross(w, d) / den;
    if (tt <= kEps || u < 0.0 || u > 1.0) return false;
    t = tt;
    n = normalized(perp(e));
    return true;
}

bool onArc(const Surface& s, Vec2 p)
{
    double a = std::atan2(p.y - s.center.y, p.x - s.center.x) - s.a0;
    a = std::fmod(a, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    return a <= s.span + 1e-12;
}

bool hitArc(const Surface& s, Vec2 o, Vec2 d, double& t, Vec2& n, bool skipNear)
{
    const Vec2 oc = o - s.center;
    const double b = dot(d, oc);
    const double c = dot(oc, oc) - s.radius * s.radius;
    const double disc = b * b - c;
    if (disc < 0) return false;
    const double sq = std::sqrt(disc);
    for (const double tt : {-b - sq, -b + sq}) {
        // Leaving this very arc, the near root is the starting point itself.
        if (tt <= (skipNear ? 1e-7 * std::max(1.0, s.radius) : kEps)) continue;
        const Vec2 p = o + d * tt;
        if (!onArc(s, p)) continue;
        t = tt;
        n = (p - s.center) / s.radius;
        return true;
    }
    return false;
}

Hit nearest(const Scene& sc, const Ray& r)
{
    Hit best;
    best.t = 1e300;
    for (int i = 0; i < int(sc.surfaces.size()); ++i) {
        const Surface& s = sc.surfaces[size_t(i)];
        double t;
        Vec2 n;
        const bool ok = s.arc ? hitArc(s, r.o, r.d, t, n, i == r.from) : (i != r.from && hitSegment(s, r.o, r.d, t, n));
        if (ok && t < best.t) {
            best.t = t;
            best.n = n;
            best.surface = i;
        }
    }
    return best;
}

} // namespace

void trace(const Scene& sc, const Options& opt, std::vector<PathSegment>& out, std::vector<Ray>& stack)
{
    out.clear();
    stack.clear();
    for (auto it = sc.rays.rbegin(); it != sc.rays.rend(); ++it) stack.push_back(*it);
    auto push = [&](const Ray& r) {
        if (r.intensity >= opt.minIntensity && r.depth < opt.maxDepth) stack.push_back(r);
    };
    while (!stack.empty() && int(out.size()) < opt.maxSegments) {
        const Ray r = stack.back();
        stack.pop_back();
        const Hit h = nearest(sc, r);
        const bool hit = h.surface >= 0;
        const Vec2 p = hit ? r.o + r.d * h.t : r.o + r.d * opt.maxLength;
        out.push_back({r.o, p, r.nm, r.intensity, {r.rgb[0], r.rgb[1], r.rgb[2]}, r.element});
        if (!hit) continue;
        const Surface& s = sc.surfaces[size_t(h.surface)];
        Ray next = r;
        next.o = p;
        next.depth = r.depth + 1;
        next.from = h.surface;
        Vec2 n = h.n;
        if (dot(n, r.d) > 0) n = -n; // facing the incoming ray
        switch (s.kind) {
        case SurfaceKind::Absorb: break;
        case SurfaceKind::Mirror:
            next.d = r.d - n * (2.0 * dot(r.d, n));
            next.intensity = float(r.intensity * s.reflectance);
            push(next);
            break;
        case SurfaceKind::ThinLens: {
            // Ideal thin lens: slope' = slope − h / f in the lens frame.
            const Vec2 axis = -n; // along the propagation
            const Vec2 along = normalized(s.b - s.a);
            const Vec2 mid = (s.a + s.b) * 0.5;
            const double hgt = dot(p - mid, along);
            const double slope = dot(r.d, along) / dot(r.d, axis);
            next.d = normalized(axis + along * (slope - hgt / s.focal));
            push(next);
            break;
        }
        case SurfaceKind::Refract: {
            const bool exiting = r.element == s.element;
            const double n1 = r.element >= 0 ? sc.elements[size_t(r.element)].index(r.nm) : 1.0;
            const double n2 = exiting ? 1.0 : sc.elements[size_t(s.element)].index(r.nm);
            const double eta = n1 / n2;
            const double cosi = -dot(r.d, n);
            const double k = 1.0 - eta * eta * (1.0 - cosi * cosi);
            Ray refl = next;
            refl.d = r.d + n * (2.0 * cosi);
            if (k < 0.0) { // total internal reflection
                push(refl);
                break;
            }
            const double cost = std::sqrt(k);
            double R = 0.0;
            if (opt.fresnel) {
                const double rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost);
                const double rp = (n1 * cost - n2 * cosi) / (n1 * cost + n2 * cosi);
                R = 0.5 * (rs * rs + rp * rp);
            }
            next.d = normalized(r.d * eta + n * (eta * cosi - cost));
            next.element = exiting ? -1 : s.element;
            next.intensity = float(r.intensity * (1.0 - R));
            refl.intensity = float(r.intensity * R);
            push(refl);
            push(next); // transmitted branch traced first
            break;
        }
        }
    }
}

} // namespace kn::optics
