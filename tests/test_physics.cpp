#include "core/Scene.h"
#include "core/Sim.h"

#include <QTest>

using namespace kn;

class TestPhysics : public QObject {
    Q_OBJECT
private slots:
    void boxRestsOnGround()
    {
        SceneData s = SceneData::empty();
        const ItemType* ground = ItemRegistry::instance().find("body.ground");
        const ItemType* box = ItemRegistry::instance().find("body.box");
        s.items.push_back(ground->makeDefault(1, {0, -0.5}));
        s.items.push_back(box->makeDefault(2, {0, 2}));
        SimStart st;
        for (auto& d : s.items) st.items.push_back(std::make_shared<ItemDef>(d));
        st.world = std::make_shared<ItemDef>(s.world);
        SimCore core;
        core.start(st);
        for (int i = 0; i < 600; ++i) {
            core.advance();
            if (i % 50 == 0) {
                Snapshot sn; core.writeSnapshot(sn);
                const BodySnap& bb = sn.bodies[1];
                qDebug() << i << "y" << bb.y << "awake" << bb.awake << "n" << bb.forceCount << "fy" << bb.fy;
            }
        }
        Snapshot snap;
        core.writeSnapshot(snap);
        QCOMPARE(int(snap.bodies.size()), 2);
        const BodySnap& b = snap.bodies[1];
        QVERIFY(std::abs(b.y - 0.5f) < 0.02f);
        qDebug() << "mass" << b.mass << "net" << b.fx << b.fy;
        for (int i = 0; i < b.forceCount; ++i)
            qDebug() << forceSourceName(ForceSource(b.forces[i].source)) << b.forces[i].fx << b.forces[i].fy;
    }
};

QTEST_GUILESS_MAIN(TestPhysics)
#include "test_physics.moc"
