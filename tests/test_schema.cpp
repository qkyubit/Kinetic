// Property schema invariants for every registered item type.
#include "core/Scene.h"
#include "items/BodyItem.h"

#include <QSet>
#include <QTest>

using namespace kn;

class TestSchema : public QObject {
    Q_OBJECT
private slots:
    void everyTypeIsWellFormed()
    {
        QSet<QString> typeIds;
        for (const ItemType* t : ItemRegistry::instance().types()) {
            const ItemMeta& m = t->meta();
            QVERIFY2(!typeIds.contains(m.typeId), qPrintable(m.typeId));
            typeIds.insert(m.typeId);
            QVERIFY(!m.name.isEmpty());
            if (m.toolbox) QVERIFY2(!m.category.isEmpty() && !m.icon.isEmpty(), qPrintable(m.typeId));
            QSet<QString> keys;
            for (const PropDef& d : t->schema().defs()) {
                const QString where = m.typeId + "." + d.key;
                QVERIFY2(!keys.contains(d.key), qPrintable(where));
                keys.insert(d.key);
                QVERIFY2(!d.label.isEmpty(), qPrintable(where));
                QVERIFY2(d.min <= d.max, qPrintable(where));
                // Defaults are valid and in range.
                QVERIFY2(clampValue(d, d.def) == d.def, qPrintable(where));
                if (d.type == PropType::Double || d.type == PropType::Int)
                    QVERIFY2(numericValue(d.def) >= d.min && numericValue(d.def) <= d.max, qPrintable(where));
                if (d.type == PropType::Enum) QVERIFY2(!d.options.isEmpty(), qPrintable(where));
                // JSON round trip of the default.
                PropValue back;
                QVERIFY2(valueFromJson(d, valueToJson(d, d.def), back), qPrintable(where));
                QVERIFY2(back == d.def, qPrintable(where));
            }
        }
    }

    void clampingAndBadJson()
    {
        PropertySchema s;
        const PropDef& n = s.num("n", "N", unit::m, 0, 10, 5);
        QCOMPARE(std::get<double>(clampValue(n, 20.0)), 10.0);
        QCOMPARE(std::get<double>(clampValue(n, -1.0)), 0.0);
        QCOMPARE(std::get<double>(clampValue(n, std::nan(""))), 5.0);
        QCOMPARE(std::get<double>(clampValue(n, int64_t(3))), 5.0); // wrong alternative -> default
        PropValue v;
        QVERIFY(!valueFromJson(n, QJsonValue("x"), v));
        const PropDef& c = s.colour("c", "C", Color::fromRgb(0x123456));
        QVERIFY(valueFromJson(c, QJsonValue("#abcdef"), v));
        QCOMPARE(std::get<Color>(v), Color::fromRgb(0xabcdef));
        QVERIFY(!valueFromJson(c, QJsonValue("#12"), v));
        const PropDef& e = s.choice("e", "E", {"A", "B"}, 0);
        QVERIFY(valueFromJson(e, QJsonValue("B"), v));
        QCOMPARE(std::get<int64_t>(v), int64_t(1));
        QVERIFY(!valueFromJson(e, QJsonValue("C"), v));
    }

    void massDensityCoupling()
    {
        const auto* box = static_cast<const BodyItemType*>(ItemRegistry::instance().find("body.box"));
        ItemDef d = box->makeDefault(1, {});
        const auto& ix = box->idx();
        // 1 m x 1 m wood box: 700 kg (implicit 1 m depth).
        QCOMPARE(d.d(ix.density), 700.0);
        QVERIFY(std::abs(d.d(ix.mass) - 700.0) < 1e-3);
        // Editing the mass updates the density.
        d.set(ix.mass, 350.0);
        box->propertyChanged(d, ix.mass);
        QVERIFY(std::abs(d.d(ix.density) - 350.0) < 1e-6);
        QCOMPARE(d.i(ix.material), int64_t(0)); // now "Custom"
        // Editing the density updates the mass.
        d.set(ix.density, 7850.0);
        box->propertyChanged(d, ix.density);
        QVERIFY(std::abs(d.d(ix.mass) - 7850.0) < 0.01);
        // Choosing a material sets density, friction and restitution.
        d.set(ix.material, int64_t(4)); // ice
        box->propertyChanged(d, ix.material);
        QCOMPARE(d.d(ix.density), 917.0);
        QCOMPARE(d.d(ix.muKinetic), 0.03);
        // Inertia of a uniform square about its centre: m (w² + h²) / 12.
        QVERIFY(std::abs(d.d(ix.inertia) - 917.0 * 2.0 / 12.0) < 1e-3);
    }
};

QTEST_GUILESS_MAIN(TestSchema)
#include "test_schema.moc"
