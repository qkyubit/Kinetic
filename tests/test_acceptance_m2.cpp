// M2 acceptance: a double pendulum, a working crank-slider, and pulley systems
// whose tensions match the analytic values within 2 %.
#include "core/SceneBuilder.h"

#include <QTest>

using namespace kn;


class TestAcceptanceM2 : public QObject {
    Q_OBJECT
private slots:
    void doublePendulum()
    {
        SceneBuilder sb;
        const Vec2 pivot{0, 6};
        const ItemId b1 = sb.add("body.circle", {2, 6}, {{"radius", 0.15}, {"density", 1000.0}});
        const ItemId b2 = sb.add("body.circle", {4, 6}, {{"radius", 0.15}, {"density", 1000.0}});
        const ItemId r1 = sb.connect("connector.rod", b1, {2, 6}, 0, pivot);
        const ItemId r2 = sb.connect("connector.rod", b1, {2, 6}, b2, {4, 6});
        QCOMPARE(sb.get<double>(r1, "length"), 2.0);
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        double maxLenErr = 0, minX2 = 1e9, maxX2 = -1e9;
        core.writeSnapshot(s);
        const double e0 = s.energy.total;
        double maxDrift = 0;
        for (int i = 0; i < 120 * 10; ++i) {
            core.advance();
            core.writeSnapshot(s);
            const BodySnap* p1 = body(s, b1);
            const BodySnap* p2 = body(s, b2);
            const Vec2 c1{p1->cx, p1->cy}, c2{p2->cx, p2->cy};
            maxLenErr = std::max({maxLenErr, std::abs(distance(c1, pivot) - 2.0), std::abs(distance(c1, c2) - 2.0)});
            minX2 = std::min(minX2, c2.x);
            maxX2 = std::max(maxX2, c2.x);
            maxDrift = std::max(maxDrift, std::abs(s.energy.total - e0) / std::abs(e0 - s.energy.gravity + 1e-9));
        }
        qInfo("double pendulum: max rod length error %.4f m, bob 2 x range [%.2f, %.2f]", maxLenErr, minX2, maxX2);
        QVERIFY(maxLenErr < 0.02);   // rods stay rigid (1 %)
        QVERIFY(minX2 < -1.0 && maxX2 > 3.0); // it actually swings through
        const ItemSnap* rod = itemSnap(s, r2);
        QVERIFY(rod && std::isfinite(rod->v[4]));
    }

    void crankSlider()
    {
        SceneBuilder sb;
        const double r = 1.0, L = 3.0, omega = 2.0;
        const Vec2 c{0, 3};
        const ItemId wheel = sb.add("body.circle", c, {{"radius", 1.1}, {"density", 200.0}});
        const ItemId block = sb.add("body.box", {c.x + r + L, c.y}, {{"width", 0.5}, {"height", 0.5}, {"density", 500.0}});
        sb.connect("mechanism.motor", wheel, c, 0, c, {{"speed", omega}, {"maxTorque", 1e7}});
        sb.connect("connector.rod", wheel, {c.x + r, c.y}, block, {c.x + r + L, c.y});
        sb.connect("connector.slider", block, {c.x + r + L, c.y}, 0, {c.x + r + L + 1.0, c.y});
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        double maxErr = 0, maxDy = 0, xmin = 1e9, xmax = -1e9;
        for (int i = 1; i <= 120 * 8; ++i) {
            core.advance();
            core.writeSnapshot(s);
            const BodySnap* w = body(s, wheel);
            const BodySnap* b = body(s, block);
            // Slider position from the crank angle (exact kinematics).
            const double theta = std::atan2(double(w->s), double(w->c));
            const double xExpected = c.x + r * std::cos(theta) + std::sqrt(L * L - std::pow(r * std::sin(theta), 2));
            if (i > 60) maxErr = std::max(maxErr, std::abs(double(b->x) - xExpected));
            maxDy = std::max(maxDy, std::abs(double(b->y) - c.y));
            xmin = std::min(xmin, double(b->x));
            xmax = std::max(xmax, double(b->x));
            if (i == 120 * 8) QVERIFY2(std::abs(double(w->w) - omega) < 0.05, qPrintable(QString::number(w->w)));
        }
        qInfo("crank-slider: stroke [%.3f, %.3f], max kinematic error %.4f m, max vertical drift %.4f m", xmin, xmax,
              maxErr, maxDy);
        QVERIFY(std::abs((xmax - xmin) - 2.0 * r) < 0.02); // full stroke = 2r
        QVERIFY(maxErr < 0.01);
        QVERIFY(maxDy < 0.005);
    }

    void atwoodTension_data()
    {
        QTest::addColumn<double>("ratio");
        QTest::newRow("atwood 1:1") << 1.0;
        QTest::newRow("block and tackle 1:2") << 2.0;
    }

    void atwoodTension()
    {
        QFETCH(double, ratio);
        SceneBuilder sb;
        const double mA = 2.0, mB = 1.0, g = 9.81, side = 0.4;
        const ItemId a = sb.add("body.box", {-0.6, 3}, {{"width", side}, {"height", side}, {"density", mA / (side * side)}});
        const ItemId b = sb.add("body.box", {0.6, 3}, {{"width", side}, {"height", side}, {"density", mB / (side * side)}});
        const ItemId p = sb.connect("connector.pulley", a, {-0.6, 3}, b, {0.6, 3}, {{"ratio", ratio}});
        sb.set(p, "groundA", Vec2{-0.6, 6});
        sb.set(p, "groundB", Vec2{0.6, 6});
        sb.set(p, "length", 3.0 + ratio * 3.0);
        QCOMPARE(sb.get<double>(a, "density") * side * side, mA);

        // Analytic (a = downward acceleration of A):
        //   ratio 1: a = g (mA - mB)/(mA + mB),            T = 2 mA mB g / (mA + mB)
        //   ratio r (B hangs on the moving block, travels 1/r as far):
        //            a = g (mA - mB/r)/(mA + mB/r²),        T = mA (g - a)
        const double aExp = g * (mA - mB / ratio) / (mA + mB / (ratio * ratio));
        const double tExp = mA * (g - aExp);

        SimCore core;
        core.start(sb.start());
        Snapshot s;
        for (int i = 0; i < 60; ++i) core.advance(); // 0.5 s
        core.writeSnapshot(s);
        const double t = itemSnap(s, p)->v[4];
        const BodySnap* ba = body(s, a);
        const BodySnap* bb = body(s, b);
        qInfo("ratio %.0f: tension %.4f N (analytic %.4f), a_A %.4f m/s² (analytic %.4f)", ratio, t, tExp, -ba->ay, aExp);
        QVERIFY(std::abs(t - tExp) / tExp < 0.02);
        QVERIFY(std::abs(-ba->ay - aExp) / aExp < 0.02);
        // The rope force shows up in the force accounting on both bodies.
        double fyA = 0, fyB = 0;
        QVERIFY(std::abs(forceOf(*ba, ForceSource::Tension, &fyA) - tExp) / tExp < 0.02);
        QVERIFY(std::abs(forceOf(*bb, ForceSource::Tension, &fyB) - ratio * tExp) / tExp < 0.02);
        QVERIFY(fyA > 0 && fyB > 0);
        // Rope length is conserved.
        QVERIFY(std::abs(itemSnap(s, p)->v[5] - (3.0 + ratio * 3.0)) < 1e-3);
    }

    void restingPulleyHoldsSteady()
    {
        // Heavy block on the floor, light block hanging: T = m_light g and nothing moves.
        SceneBuilder sb;
        sb.add("body.ground", {0, -0.5});
        const double side = 0.4;
        const ItemId heavy = sb.add("body.box", {-0.6, side / 2}, {{"width", side}, {"height", side}, {"density", 5 / (side * side)}});
        const ItemId light = sb.add("body.box", {0.6, 2}, {{"width", side}, {"height", side}, {"density", 1 / (side * side)}});
        const ItemId p = sb.connect("connector.pulley", heavy, {-0.6, side / 2}, light, {0.6, 2});
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        for (int i = 0; i < 240; ++i) core.advance();
        core.writeSnapshot(s);
        const double t = itemSnap(s, p)->v[4];
        qInfo("resting pulley: T = %.3f N (expected %.3f), light block vy %.4f", t, 9.81, body(s, light)->vy);
        QVERIFY(std::abs(t - 9.81) / 9.81 < 0.02);
        QVERIFY(std::abs(body(s, light)->vy) < 0.01);
    }

    void gearRatio()
    {
        SceneBuilder sb;
        const ItemId g1 = sb.add("body.circle", {0, 3}, {{"radius", 0.5}});
        const ItemId g2 = sb.add("body.circle", {1.5, 3}, {{"radius", 1.0}, {"layers", int64_t(2)}, {"mask", int64_t(2)}});
        sb.set(g1, "layers", int64_t(1));
        sb.set(g1, "mask", int64_t(1));
        sb.connect("mechanism.motor", g1, {0, 3}, 0, {0, 3}, {{"speed", 4.0}, {"maxTorque", 1e7}});
        sb.connect("connector.hinge", g2, {1.5, 3}, 0, {1.5, 3});
        sb.connect("connector.gear", g1, {0, 3}, g2, {1.5, 3}, {{"ratio", 2.0}});
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        for (int i = 0; i < 240; ++i) core.advance();
        core.writeSnapshot(s);
        const double w1 = body(s, g1)->w, w2 = body(s, g2)->w;
        qInfo("gear: w1 %.4f rad/s, w2 %.4f rad/s (expected -w1/2)", w1, w2);
        QVERIFY(std::abs(w1 - 4.0) < 0.05);
        QVERIFY(std::abs(w2 + w1 / 2.0) < 0.02);
    }

    void springOscillator()
    {
        // Mass on a spring from a fixed point: period 2π sqrt(m/k), energy conserved.
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        const double m = 2.0, k = 200.0, side = 0.2;
        const ItemId box = sb.add("body.box", {1.5, 0}, {{"width", side}, {"height", side}, {"density", m / (side * side)}});
        const ItemId sp = sb.connect("connector.spring", 0, {0, 0}, box, {1.5, 0}, {{"stiffness", k}});
        sb.set(sp, "restLength", 1.0);
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        core.writeSnapshot(s);
        const double e0 = s.energy.total;
        QVERIFY(std::abs(e0 - 0.5 * k * 0.25) < 1e-6);
        // Find the period from successive upward zero crossings of the displacement.
        double prev = 0.5, firstCross = -1, lastCross = -1;
        int crossings = 0;
        double maxDrift = 0;
        for (int i = 1; i <= 120 * 10; ++i) {
            core.advance();
            core.writeSnapshot(s);
            const double x = body(s, box)->cx - 1.0;
            if (prev < 0 && x >= 0) {
                const double t = s.time - s.dt * x / (x - prev);
                if (firstCross < 0) firstCross = t;
                lastCross = t;
                ++crossings;
            }
            prev = x;
            maxDrift = std::max(maxDrift, std::abs(s.energy.total - e0) / e0);
        }
        const double period = (lastCross - firstCross) / (crossings - 1);
        const double expected = 2 * kPi * std::sqrt(m / k);
        qInfo("spring: period %.5f s (analytic %.5f), max energy deviation %.3f %%", period, expected, maxDrift * 100);
        QVERIFY(std::abs(period - expected) / expected < 0.01);
        QVERIFY(maxDrift < 0.02);
    }
};

QTEST_GUILESS_MAIN(TestAcceptanceM2)
#include "test_acceptance_m2.moc"
