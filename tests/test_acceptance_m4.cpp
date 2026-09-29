// M4 acceptance: floating bodies settle at the analytic waterline within 1 %,
// and the field items reproduce textbook results.
#include "core/Optics.h"
#include "core/SceneBuilder.h"

#include <QTest>

using namespace kn;

namespace {

void run(SimCore& core, int steps)
{
    for (int i = 0; i < steps; ++i) core.advance();
}

Snapshot snap(const SimCore& core)
{
    Snapshot s;
    core.writeSnapshot(s);
    return s;
}

// Optics scene from the builder's items.
optics::Scene opticsScene(const SceneBuilder& sb)
{
    optics::Scene sc;
    for (const ItemDef& d : sb.scene.items) d.type->addOptics(d, d.type->pose(d), sc);
    return sc;
}

// Last segment of each traced path (paths are emitted depth-first, one after another).
std::vector<optics::PathSegment> finalSegments(const optics::Scene& sc, const optics::Options& o)
{
    std::vector<optics::PathSegment> out, last;
    std::vector<optics::Ray> stack;
    optics::trace(sc, o, out, stack);
    for (size_t i = 0; i < out.size(); ++i)
        if (i + 1 == out.size() || distance(out[i + 1].a, out[i].b) > 1e-9) last.push_back(out[i]);
    return last;
}

// Where a segment's line crosses y = 0.
double axisCrossing(const optics::PathSegment& s)
{
    return s.a.x - s.a.y * (s.b.x - s.a.x) / (s.b.y - s.a.y);
}

} // namespace

class TestAcceptanceM4 : public QObject {
    Q_OBJECT
private slots:
    void waterline_data()
    {
        QTest::addColumn<QByteArray>("shape");
        QTest::addColumn<double>("density");
        QTest::addColumn<int>("liquid");
        QTest::newRow("pine box in water") << QByteArray("body.box") << 500.0 << 1;
        QTest::newRow("oak box in water") << QByteArray("body.box") << 750.0 << 1;
        QTest::newRow("steel box in mercury") << QByteArray("body.box") << 7850.0 << 4;
        QTest::newRow("ice box in sea water") << QByteArray("body.box") << 917.0 << 2;
        QTest::newRow("cork ball in water") << QByteArray("body.circle") << 240.0 << 1;
        QTest::newRow("ball in oil") << QByteArray("body.circle") << 455.5 << 3;
    }

    void waterline()
    {
        QFETCH(QByteArray, shape);
        QFETCH(double, density);
        QFETCH(int, liquid);
        SceneBuilder sb;
        const double surface = 3.0;
        const ItemId tank = sb.add("fluid.region", {0, surface - 2.0}, {{"width", 8.0}, {"height", 4.0}, {"liquid", int64_t(liquid)}});
        const double rhoF = sb.get<double>(tank, "density");
        const bool box = shape == "body.box";
        const ItemId b = box ? sb.add(shape.constData(), {0, surface + 0.2}, {{"width", 1.0}, {"height", 0.8}})
                             : sb.add(shape.constData(), {0, surface + 0.2}, {{"radius", 0.5}});
        sb.set(b, "density", density);
        sb.setWorld("sleep", false);
        SimCore core;
        core.start(sb.start());
        run(core, 120 * 30);
        // Average over the last 5 s to remove any residual bobbing.
        double y = 0;
        const int n = 600;
        for (int i = 0; i < n; ++i) {
            core.advance();
            y += body(snap(core), b)->y;
        }
        y /= n;
        const double ratio = density / rhoF;
        double expected, size;
        if (box) {
            // Draft d = h ρb/ρf: centre sits at surface − d + h/2.
            size = 0.8;
            expected = surface - size * ratio + size / 2;
        } else {
            // Submerged segment area fraction = ρb/ρf; solve for the centre height.
            const double r = 0.5;
            size = 2 * r;
            double lo = -r, hi = r; // centre offset above the surface
            for (int it = 0; it < 100; ++it) {
                const double m = 0.5 * (lo + hi);
                const double t = std::acos(std::clamp(m / r, -1.0, 1.0));
                const double seg = r * r * (t - std::sin(t) * std::cos(t)); // area below the surface
                (seg / (kPi * r * r) > ratio ? lo : hi) = m;
            }
            expected = surface + 0.5 * (lo + hi);
        }
        const double draftSim = surface - (y - size / 2), draftAnalytic = surface - (expected - size / 2);
        const double err = std::abs(draftSim - draftAnalytic) / draftAnalytic;
        qInfo("%s ρ=%g in ρf=%g: draft %.4f m (analytic %.4f m), error %.3f %%", shape.constData(), density, rhoF, draftSim,
              draftAnalytic, err * 100);
        QVERIFY(err < 0.01);
    }

    void chargedParticleCircle()
    {
        // A charge in a uniform B field moves on a circle of radius m v / (|q| B) at constant speed.
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.add("field.magnetic", {0, 0}, {{"region", int64_t(0)}, {"strength", 2e4}});
        const ItemId p = sb.add("body.circle", {0, 0}, {{"radius", 0.1}, {"charge", 1e-3}, {"velocity", Vec2{3, 0}}});
        SimCore core;
        core.start(sb.start());
        const double m = body(snap(core), p)->mass;
        const double R = m * 3.0 / (1e-3 * 2e4);
        double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9, vmin = 1e9, vmax = 0;
        for (int i = 0; i < 120 * 10; ++i) {
            core.advance();
            const Snapshot s = snap(core);
            const BodySnap* b = body(s, p);
            xmin = std::min(xmin, double(b->x));
            xmax = std::max(xmax, double(b->x));
            ymin = std::min(ymin, double(b->y));
            ymax = std::max(ymax, double(b->y));
            const double v = std::hypot(b->vx, b->vy);
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        const double rx = 0.5 * (xmax - xmin), ry = 0.5 * (ymax - ymin);
        qInfo("cyclotron: radius %.4f / %.4f m (analytic %.4f), speed %.5f–%.5f m/s", rx, ry, R, vmin, vmax);
        QVERIFY(std::abs(rx - R) / R < 0.01);
        QVERIFY(std::abs(ry - R) / R < 0.01);
        QVERIFY(vmax - vmin < 3e-3 * 3.0);
        // The field bends clockwise for positive q and B out of the screen: centre below the start.
        QVERIFY(ymin < -R);
    }

    void coulombForce()
    {
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        const ItemId a = sb.add("body.circle", {0, 0}, {{"radius", 0.1}, {"charge", 1e-4}});
        const ItemId b = sb.add("body.circle", {2, 0}, {{"radius", 0.1}, {"charge", -2e-4}});
        SimCore core;
        core.start(sb.start());
        core.advance();
        const Snapshot s = snap(core);
        const double eps = 0.05, r = 2.0;
        const double expected = 8.9875517923e9 * 1e-4 * 2e-4 * r / std::pow(r * r + eps * eps, 1.5);
        double fyA = 0, fyB = 0;
        const double fa = forceOf(*body(s, a), ForceSource::Electric, &fyA);
        const double fb = forceOf(*body(s, b), ForceSource::Electric, &fyB);
        qInfo("Coulomb: %.4f N and %.4f N (analytic %.4f N)", fa, fb, expected);
        QVERIFY(std::abs(fa - expected) / expected < 1e-3);
        QVERIFY(std::abs(fb - expected) / expected < 1e-3);
        QVERIFY(body(s, a)->vx > 0 && body(s, b)->vx < 0); // opposite charges attract
    }

    void terminalVelocity()
    {
        // Quadratic air drag: v_t = sqrt(2 m g / (ρ Cd W)).
        SceneBuilder sb;
        sb.setWorld("airDrag", true);
        sb.setWorld("sleep", false);
        const ItemId p = sb.add("body.circle", {0, 0}, {{"radius", 0.1}, {"density", 20.0}});
        SimCore core;
        core.start(sb.start());
        const double m = body(snap(core), p)->mass;
        const double vt = std::sqrt(2 * m * 9.81 / (1.225 * 1.0 * 0.2));
        run(core, 120 * 20);
        const double v = -body(snap(core), p)->vy;
        qInfo("terminal velocity %.4f m/s (analytic %.4f)", v, vt);
        QVERIFY(std::abs(v - vt) / vt < 0.005);
    }

    void twoBodyOrbit()
    {
        // Mutual gravity: a light body on a circular orbit around a heavy one.
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.setWorld("mutualGravity", true);
        sb.setWorld("gravitationalConstant", 1.0);
        sb.setWorld("coulombSoftening", 0.001);
        sb.setWorld("sleep", false);
        const ItemId sun = sb.add("body.circle", {0, 0}, {{"radius", 0.3}, {"density", 3000.0}});
        const ItemId planet = sb.add("body.circle", {4, 0}, {{"radius", 0.05}, {"density", 100.0}});
        const double M = sb.get<double>(sun, "mass"), m = sb.get<double>(planet, "mass");
        const double r = 4.0, mu = 1.0 * (M + m);
        const double v = std::sqrt(mu / r); // relative speed
        // Momentum-free start: velocities split by mass.
        sb.set(planet, "velocity", Vec2{0, v * M / (M + m)});
        sb.set(sun, "velocity", Vec2{0, -v * m / (M + m)});
        SimCore core;
        core.start(sb.start());
        const double T = 2 * kPi * std::sqrt(r * r * r / mu);
        const double e0 = snap(core).energy.total;
        double worst = 0, rmin = 1e9, rmax = 0;
        const int steps = int(std::lround(3 * T * 120));
        for (int i = 0; i < steps; ++i) {
            core.advance();
            const Snapshot s = snap(core);
            worst = std::max(worst, std::abs(s.energy.total - e0));
            const BodySnap *a = body(s, sun), *b = body(s, planet);
            const double d = std::hypot(b->x - a->x, b->y - a->y);
            rmin = std::min(rmin, d);
            rmax = std::max(rmax, d);
        }
        const double scale = 0.5 * m * v * v;
        qInfo("orbit: T = %.3f s, r in [%.4f, %.4f], max energy error %.4f %% of planet KE", T, rmin, rmax, 100 * worst / scale);
        QVERIFY(rmax - rmin < 0.02 * r);
        QVERIFY(worst / scale < 0.005);
    }

    void explosionImpulse()
    {
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.add("field.explosion", {0, 0}, {{"time", 0.5}, {"radius", 4.0}, {"impulse", 1000.0}});
        const ItemId b = sb.add("body.circle", {1, 0}, {{"radius", 0.2}});
        SimCore core;
        core.start(sb.start(true));
        run(core, 120);
        const Snapshot s = snap(core);
        const double m = body(s, b)->mass;
        const double expected = 1000.0 * (1 - 1.0 / 4.0) / m;
        qInfo("explosion: v = %.5f m/s (analytic %.5f)", double(body(s, b)->vx), expected);
        QVERIFY(std::abs(body(s, b)->vx - expected) / expected < 1e-3);
        // Scrubbing back before the detonation and replaying fires it exactly once.
        core.scrubTo(30);
        QCOMPARE(double(body(snap(core), b)->vx), 0.0);
        run(core, 90);
        QVERIFY(std::abs(body(snap(core), b)->vx - expected) / expected < 1e-3);
    }

    void thrusterAcceleration()
    {
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        const ItemId b = sb.add("body.box", {0, 0}, {{"width", 1.0}, {"height", 1.0}});
        // Nozzle at the bottom face, pushing up (+y).
        sb.connect("mechanism.thruster", b, {0, -0.5}, 0, {0, 1}, {{"thrust", 700.0}});
        SimCore core;
        core.start(sb.start());
        const double m = sb.get<double>(b, "mass");
        run(core, 120);
        const Snapshot s = snap(core);
        qInfo("thruster: vy = %.5f m/s after 1 s (analytic %.5f), w = %.2e", double(body(s, b)->vy), 700.0 / m, double(body(s, b)->w));
        QVERIFY(std::abs(body(s, b)->vy - 700.0 / m) / (700.0 / m) < 1e-3);
        QVERIFY(std::abs(body(s, b)->w) < 1e-4);
    }

    void lensFocalLength_data()
    {
        QTest::addColumn<double>("r1");
        QTest::addColumn<double>("r2");
        QTest::addColumn<double>("t");
        QTest::addColumn<int>("glass");
        QTest::newRow("biconvex N-BK7") << 0.5 << -0.5 << 0.05 << 1;
        QTest::newRow("plano-convex fused silica") << 0.8 << 0.0 << 0.08 << 4;
        QTest::newRow("thick meniscus SF11") << 0.4 << 1.2 << 0.12 << 3;
    }

    void lensFocalLength()
    {
        // Back focal distance of paraxial rays vs the thick-lens lensmaker equation.
        QFETCH(double, r1);
        QFETCH(double, r2);
        QFETCH(double, t);
        QFETCH(int, glass);
        SceneBuilder sb;
        const ItemId lens = sb.add("optics.lens", {0, 0},
                                   {{"radius1", r1}, {"radius2", r2}, {"thickness", t}, {"aperture", 0.3}, {"glass", int64_t(glass)}});
        optics::Scene sc = opticsScene(sb);
        for (const double h : {0.001, -0.002, 0.004}) sc.emitRay({-2, h}, {1, 0}, 587.6, 1.0);
        optics::Options o;
        o.fresnel = false;
        const auto last = finalSegments(sc, o);
        QCOMPARE(last.size(), size_t(3));
        const double n = optics::glassPreset(glass).index(587.6);
        const double c1 = 1 / r1, c2 = r2 != 0 ? 1 / r2 : 0.0;
        const double f = 1 / ((n - 1) * (c1 - c2 + (n - 1) * t * c1 * c2 / n));
        const double bfl = f * (1 - (n - 1) * t * c1 / n);
        QVERIFY(std::abs(sb.get<double>(lens, "focalLength") - f) < 1e-9);
        for (const auto& seg : last) {
            const double measured = axisCrossing(seg) - 0.5 * t;
            qInfo("n = %.5f, f = %.5f m: back focal distance %.5f m (lensmaker %.5f), error %.4f %%", n, f, measured, bfl,
                  100 * std::abs(measured - bfl) / bfl);
            QVERIFY(std::abs(measured - bfl) / bfl < 0.02);
        }
    }

    void prismDispersion()
    {
        // Deviation through a 60° dense-flint prism vs δ = θ1 + θ4 − A, per wavelength.
        SceneBuilder sb;
        const double A = 60.0 * kRadPerDeg, L = 1.2;
        sb.add("optics.prism", {0, 0}, {{"apexAngle", A}, {"side", L}, {"glass", int64_t(3)}});
        const double hb = L * std::sin(A / 2), H = L * std::cos(A / 2);
        const Vec2 apex{0, 2 * H / 3}, left{-hb, -H / 3};
        const Vec2 inward = normalized(Vec2{H, -hb});
        const double theta1 = 65.0 * kRadPerDeg;
        const Vec2 d = rotate(inward, theta1);
        const Vec2 mid = (apex + left) * 0.5;
        std::vector<double> dev;
        for (const double nm : {400.0, 550.0, 700.0}) {
            optics::Scene sc = opticsScene(sb);
            sc.emitRay(mid - d * 2.0, d, nm, 1.0);
            optics::Options o;
            o.fresnel = false;
            const auto last = finalSegments(sc, o);
            QCOMPARE(last.size(), size_t(1));
            const Vec2 out = normalized(last[0].b - last[0].a);
            const double delta = std::acos(std::clamp(dot(out, d), -1.0, 1.0));
            const double n = optics::glassPreset(3).index(nm);
            const double t2 = std::asin(std::sin(theta1) / n), t4 = std::asin(n * std::sin(A - t2));
            const double expected = theta1 + t4 - A;
            qInfo("%g nm: n = %.5f, deviation %.4f° (analytic %.4f°)", nm, n, delta * kDegPerRad, expected * kDegPerRad);
            QVERIFY(std::abs(delta - expected) < 0.01 * kRadPerDeg);
            dev.push_back(delta);
        }
        const double spread = (dev[0] - dev[2]) * kDegPerRad;
        qInfo("angular spread 400–700 nm: %.3f°", spread);
        QVERIFY(dev[0] > dev[1] && dev[1] > dev[2]); // violet bends most
        QVERIFY(spread > 2.0);
    }

    void mirrorAndThinLensFocus()
    {
        SceneBuilder sb;
        sb.add("optics.curvedMirror", {0, 0}, {{"radius", 3.0}, {"aperture", 1.0}});
        optics::Scene sc = opticsScene(sb);
        sc.emitRay({5, 0.01}, {-1, 0}, 550, 1.0);
        auto last = finalSegments(sc, {});
        QCOMPARE(last.size(), size_t(1));
        qInfo("concave mirror R = 3: focus at %.6f m (paraxial 1.5)", axisCrossing(last[0]));
        QVERIFY(std::abs(axisCrossing(last[0]) - 1.5) < 1e-4);

        SceneBuilder lb;
        lb.add("optics.thinLens", {0, 0}, {{"focalLength", 1.5}, {"aperture", 1.0}});
        sc = opticsScene(lb);
        sc.emitRay({-3, 0.3}, {1, 0}, 550, 1.0);
        sc.emitRay({3, -0.2}, {-1, 0}, 550, 1.0); // from the other side
        last = finalSegments(sc, {});
        QCOMPARE(last.size(), size_t(2));
        QVERIFY(std::abs(axisCrossing(last[0]) - 1.5) < 1e-9);
        QVERIFY(std::abs(axisCrossing(last[1]) + 1.5) < 1e-9);
    }

    void fresnelAndTotalInternalReflection()
    {
        SceneBuilder sb;
        sb.add("optics.block", {0, 0}, {{"width", 1.0}, {"height", 1.0}, {"glass", int64_t(1)}});
        optics::Scene sc = opticsScene(sb);
        sc.emitRay({-2, 0.1}, {1, 0}, 587.6, 1.0); // normal incidence
        std::vector<optics::PathSegment> out;
        std::vector<optics::Ray> stack;
        optics::Options o;
        o.minIntensity = 1e-6f;
        o.maxDepth = 3;
        optics::trace(sc, o, out, stack);
        const double n = optics::glassPreset(1).index(587.6);
        const double R = std::pow((n - 1) / (n + 1), 2);
        float reflected = -1, transmitted = -1;
        for (const auto& s : out) {
            if (s.a.x == -0.5 && s.b.x < -0.5) reflected = s.intensity;
            if (s.a.x == -0.5 && s.b.x > -0.5) transmitted = s.intensity;
        }
        qInfo("normal incidence: R = %.5f (analytic %.5f), T = %.5f", double(reflected), R, double(transmitted));
        QVERIFY(std::abs(reflected - R) < 1e-6);
        QVERIFY(std::abs(reflected + transmitted - 1.0) < 1e-6);

        // Inside the glass, 60° from the normal of the top face: beyond the critical angle.
        sc = opticsScene(sb);
        sc.emitRay({-0.4, 0}, {std::sin(60 * kRadPerDeg), std::cos(60 * kRadPerDeg)}, 587.6, 1.0);
        sc.rays.back().element = 0;
        o.maxDepth = 2;
        optics::trace(sc, o, out, stack);
        QCOMPARE(out.size(), size_t(2));
        QCOMPARE(out[1].intensity, 1.0f);
        QVERIFY(out[1].b.y < out[1].a.y); // reflected back down
        QCOMPARE(out[1].element, 0);
    }

    void magnetsAttract()
    {
        // Two aligned magnets (N facing S) attract with equal and opposite forces.
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        const ItemId a = sb.add("body.magnet", {0, 0});
        const ItemId b = sb.add("body.magnet", {1.5, 0});
        SimCore core;
        core.start(sb.start());
        core.advance();
        const Snapshot s = snap(core);
        const double fa = forceOf(*body(s, a), ForceSource::Magnetic), fb = forceOf(*body(s, b), ForceSource::Magnetic);
        // On-axis dipoles: F = 6 μ0/(4π) m² / r⁴ (softened).
        const double mm = 3e4, d2 = 1.5 * 1.5 + 0.05 * 0.05;
        const double expected = 6e-7 * mm * mm / (d2 * d2);
        qInfo("magnets: %.4f N / %.4f N (analytic %.4f N)", fa, fb, expected);
        QVERIFY(std::abs(fa - expected) / expected < 1e-3);
        QVERIFY(std::abs(fb - expected) / expected < 1e-3);
        QVERIFY(body(s, a)->vx > 0 && body(s, b)->vx < 0);
    }
};

QTEST_GUILESS_MAIN(TestAcceptanceM4)
#include "test_acceptance_m4.moc"
