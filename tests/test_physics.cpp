// Physics regression tests (headless).
#include "core/AllocCounter.h"
#include "core/SceneBuilder.h"

#include <QTest>

using namespace kn;

class TestPhysics : public QObject {
    Q_OBJECT
private slots:
    void restingBoxForceBalance()
    {
        // A box at rest: the contact normal force reported from Box2D's impulses
        // equals its weight, and the net force is zero.
        SceneBuilder sb;
        sb.add("body.ground", {0, -0.5});
        const ItemId box = sb.add("body.box", {0, 0.5});
        SimCore core;
        core.start(sb.start());
        for (int i = 0; i < 200; ++i) core.advance();
        Snapshot s;
        core.writeSnapshot(s);
        const BodySnap* b = body(s, box);
        QVERIFY(std::abs(b->y - 0.5f) < 0.01f);
        double fy = 0;
        const double normal = forceOf(*b, ForceSource::Normal, &fy);
        QVERIFY(std::abs(normal - 700 * 9.81) / (700 * 9.81) < 1e-3);
        QVERIFY(fy > 0);
        QVERIFY(std::hypot(b->fx, b->fy) < 1e-2 * 700 * 9.81);
    }

    void staticVersusKineticFriction()
    {
        // A block on a 25° slope: holds with μs = 0.6 (tan 25° = 0.47), slides with
        // μs = 0.4; once sliding, acceleration = g (sin θ − μk cos θ).
        for (double muS : {0.6, 0.4}) {
            SceneBuilder sb;
            const double th = 25.0 / kDegPerRad, muK = 0.3;
            sb.add("body.box", {0, 0}, {{"width", 20.0}, {"height", 1.0}, {"bodyType", int64_t(0)}, {"angle", -th},
                                        {"staticFriction", muS}, {"kineticFriction", muK}});
            const Vec2 n{std::sin(th), std::cos(th)}; // slope normal
            // Start in contact and at rest (dropping it would make it land sliding,
            // and μk < tan θ could then never stop it).
            const ItemId box = sb.add("body.box", n * 0.75, {{"width", 0.5}, {"height", 0.5}, {"angle", -th},
                                                            {"staticFriction", muS}, {"kineticFriction", muK}});
            SimCore core;
            core.start(sb.start());
            Snapshot s;
            for (int i = 0; i < 120; ++i) core.advance();
            core.writeSnapshot(s);
            const double v1 = std::hypot(body(s, box)->vx, body(s, box)->vy);
            for (int i = 0; i < 60; ++i) core.advance();
            core.writeSnapshot(s);
            const double v2 = std::hypot(body(s, box)->vx, body(s, box)->vy);
            if (muS > 0.5) {
                QVERIFY2(v2 < 1e-3, qPrintable(QString::number(v2)));
            } else {
                const double a = (v2 - v1) / 0.5;
                const double expected = 9.81 * (std::sin(th) - muK * std::cos(th));
                qInfo("sliding block: a = %.4f m/s² (analytic %.4f)", a, expected);
                QVERIFY(std::abs(a - expected) / expected < 0.02);
            }
        }
    }

    void stepDoesNotAllocate()
    {
        // Every item type that contributes per-step work, after warm-up: the
        // physics step and the snapshot publish make no heap allocations in
        // Kinetic code (Box2D may still grow its internal pools).
        QVERIFY(alloc::available());
        SceneBuilder sb;
        sb.add("body.ground", {0, -0.5}, {{"width", 60.0}});
        std::vector<ItemId> boxes;
        for (int i = 0; i < 40; ++i) boxes.push_back(sb.add("body.box", {-10.0 + (i % 10) * 0.6, 0.5 + (i / 10) * 0.6}, {{"width", 0.5}, {"height", 0.5}}));
        const ItemId a = sb.add("body.circle", {3, 4});
        const ItemId b = sb.add("body.circle", {5, 4});
        const ItemId c = sb.add("body.box", {8, 3});
        const ItemId d = sb.add("body.box", {10, 3});
        sb.connect("connector.spring", 0, {3, 7}, a, {3, 4});
        sb.connect("connector.damper", a, {3, 4}, b, {5, 4});
        sb.connect("connector.rope", 0, {5, 7}, b, {5, 4});
        sb.connect("connector.rod", c, {8, 3}, d, {10, 3});
        sb.connect("connector.hinge", c, {8, 3}, 0, {8, 3}, {{"enableMotor", true}});
        sb.connect("connector.slider", d, {10, 3}, 0, {11, 3});
        const ItemId e = sb.add("body.box", {14, 2});
        const ItemId f = sb.add("body.box", {16, 2});
        sb.connect("connector.pulley", e, {14, 2}, f, {16, 2});
        const ItemId g1 = sb.add("body.circle", {20, 3}, {{"mask", int64_t(0)}});
        const ItemId g2 = sb.add("body.circle", {22, 3}, {{"mask", int64_t(0)}});
        sb.connect("mechanism.motor", g1, {20, 3}, 0, {20, 3});
        sb.connect("connector.hinge", g2, {22, 3}, 0, {22, 3});
        sb.connect("connector.gear", g1, {20, 3}, g2, {22, 3});
        const ItemId w = sb.add("body.box", {-15, 2});
        sb.connect("connector.weld", w, {-15, 2}, boxes[0], {-10, 0.5});
        const ItemId p = sb.add("body.box", {-20, 1});
        sb.connect("mechanism.piston", 0, {-20, -0.2}, p, {-20, 1});
        sb.add("mechanism.conveyor", {25, 1});
        sb.add("mechanism.trampoline", {-25, 0.2});

        SimCore core;
        core.start(sb.start());
        Snapshot snap;
        for (int i = 0; i < 600; ++i) core.advance();
        core.writeSnapshot(snap);
        uint64_t ours = 0, box2d = 0, publish = 0;
        for (int i = 0; i < 600; ++i) {
            core.advance();
            ours += core.world()->lastStepAllocations() - core.world()->lastStepBox2dAllocations();
            box2d += core.world()->lastStepBox2dAllocations();
            const alloc::Scope scope;
            core.writeSnapshot(snap);
            publish += scope.count();
        }
        qInfo("allocations over 600 steps: Kinetic %llu, Box2D internal %llu, snapshot %llu", (unsigned long long)ours,
              (unsigned long long)box2d, (unsigned long long)publish);
        QCOMPARE(ours, uint64_t(0));
        QCOMPARE(publish, uint64_t(0));
    }

    void determinismAcrossRuns()
    {
        // Same scene and inputs: bit-identical state, with and without recording.
        auto run = [](bool record) {
            SceneBuilder sb;
            sb.add("body.ground", {0, -0.5});
            for (int i = 0; i < 30; ++i)
                sb.add(i % 2 ? "body.circle" : "body.box", {(i % 6) * 0.9 - 2.0, 1.0 + (i / 6) * 1.1});
            const ItemId a = sb.add("body.circle", {4, 5});
            sb.connect("connector.spring", 0, {4, 8}, a, {4, 5});
            SimCore core;
            core.start(sb.start(record));
            for (int i = 0; i < 1000; ++i) core.advance();
            RestoreState rs;
            core.world()->capture(rs);
            return rs;
        };
        QVERIFY(run(false) == run(false));
        QVERIFY(run(true) == run(true));
    }
};

QTEST_GUILESS_MAIN(TestPhysics)
#include "test_physics.moc"
