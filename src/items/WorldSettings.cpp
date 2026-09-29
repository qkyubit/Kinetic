// Scene-wide simulation settings. Exactly one per scene; edited in the
// Properties panel when nothing is selected.
#include "core/Item.h"

namespace kn {

class WorldSettingsItem final : public ItemType {
public:
    WorldSettingsItem()
    {
        meta_ = {QStringLiteral("world"), QStringLiteral("World"), QString(), QStringLiteral("applications-science"),
                 QStringLiteral(":/icons/tool.svg"), {}, QStringLiteral("Scene-wide settings."), ItemKind::World,
                 CreateMode::Place, false};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Gravity"));
        s.vec(QStringLiteral("gravity"), QStringLiteral("Gravity"), unit::mps2, -100, 100, {0.0, -9.81});
        s.beginGroup(QStringLiteral("Integration"));
        s.integer(QStringLiteral("physicsHz"), QStringLiteral("Physics rate"), 30, 1000, 120)
            .tip(QStringLiteral("Fixed physics steps per simulated second."));
        s.integer(QStringLiteral("substeps"), QStringLiteral("Sub-steps"), 1, 16, 4)
            .tip(QStringLiteral("Box2D solver sub-steps per physics step."));
        s.boolean(QStringLiteral("sleep"), QStringLiteral("Allow sleeping"), true);
        s.boolean(QStringLiteral("continuous"), QStringLiteral("Continuous collision"), true);
        s.num(QStringLiteral("jointHertz"), QStringLiteral("Joint stiffness"), unit::hz, 5, 1000, 60, 1)
            .log()
            .tip(QStringLiteral("Constraint stiffness of joints (Box2D soft step). Capped at a quarter of the "
                                "sub-step rate."));
        s.num(QStringLiteral("jointDamping"), QStringLiteral("Joint damping ratio"), unit::none, 0, 20, 2, 2);
        s.num(QStringLiteral("contactHertz"), QStringLiteral("Contact stiffness"), unit::hz, 1, 1000, 30, 1)
            .log()
            .tip(QStringLiteral("Contact softness of Box2D's soft-step solver (higher = stiffer, capped by the sub-step "
                                "rate)."));
        s.num(QStringLiteral("contactDamping"), QStringLiteral("Contact damping ratio"), unit::none, 0, 100, 10, 2);
        s.num(QStringLiteral("restitutionThreshold"), QStringLiteral("Restitution threshold"), unit::mps, 0, 10, 1, 3)
            .tip(QStringLiteral("Impacts slower than this are treated as perfectly inelastic (Box2D's default is 1 m/s)."));
        s.beginGroup(QStringLiteral("Environment"));
        s.num(QStringLiteral("airDensity"), QStringLiteral("Air density"), unit::density, 0, 20, 1.225, 3)
            .tip(QStringLiteral("Used by wind and fans."));
        s.num(QStringLiteral("coulombSoftening"), QStringLiteral("Coulomb softening"), unit::m, 0.001, 1, 0.05, 3)
            .tip(QStringLiteral("Plummer softening length for electric and point-gravity forces."));
        s.beginGroup(QStringLiteral("Replay"));
        s.integer(QStringLiteral("keyframeInterval"), QStringLiteral("Keyframe interval (steps)"), 10, 100000, 240)
            .tip(QStringLiteral("While recording, the full state is stored every N steps and the world is rebuilt "
                                "from it, so any frame can be reproduced exactly."));
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

KINETIC_DEFINE_ITEM(WorldSettingsItem)

} // namespace kn
