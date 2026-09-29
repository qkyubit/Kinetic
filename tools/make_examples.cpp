// Generates the bundled example scenes (resources/examples/*.json) from code,
// so they stay valid as the item schemas evolve. Usage: kinetic-make-examples <dir>
#include "core/SceneBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cstdio>
#include <functional>

using namespace kn;

namespace {

void ground(SceneBuilder& sb, double width = 40.0, double y = -0.5)
{
    const ItemId g = sb.add("body.ground", {0, y}, {{"width", width}});
    sb.item(g).name = QStringLiteral("Ground");
}

void view(SceneBuilder& sb, Vec2 c, double ppm)
{
    sb.scene.view.center = c;
    sb.scene.view.pixelsPerMetre = ppm;
}

void name(SceneBuilder& sb, ItemId id, const char* n) { sb.item(id).name = QString::fromUtf8(n); }

void pendulum(SceneBuilder& sb)
{
    const Vec2 pivot{0, 5};
    const ItemId bob = sb.add("body.circle", {3, 5}, {{"radius", 0.25}, {"material", int64_t(1)}, {"trail", true}});
    name(sb, bob, "Bob");
    const ItemId rod = sb.connect("connector.rod", bob, {3, 5}, 0, pivot);
    name(sb, rod, "String");
    sb.setWorld("jointHertz", 240.0);
    view(sb, {0, 3}, 70);
}

void doublePendulum(SceneBuilder& sb)
{
    const Vec2 pivot{0, 6};
    const ItemId a = sb.add("body.circle", {2, 6}, {{"radius", 0.2}, {"material", int64_t(1)}, {"trail", true},
                                                    {"trailColor", Color::fromRgb(0x3daee9)}});
    const ItemId b = sb.add("body.circle", {4, 6}, {{"radius", 0.2}, {"material", int64_t(1)}, {"trail", true},
                                                    {"trailLength", 20.0}});
    name(sb, a, "Upper bob");
    name(sb, b, "Lower bob");
    sb.connect("connector.rod", a, {2, 6}, 0, pivot);
    sb.connect("connector.rod", a, {2, 6}, b, {4, 6});
    sb.setWorld("jointHertz", 240.0);
    view(sb, {0, 4}, 55);
}

void crankSlider(SceneBuilder& sb)
{
    const Vec2 c{0, 3};
    const double r = 1.0, L = 3.0;
    const ItemId wheel = sb.add("body.circle", c, {{"radius", 1.2}, {"density", 300.0}, {"color", Color::fromRgb(0x95a5a6)}});
    const ItemId block = sb.add("body.box", {r + L, c.y}, {{"width", 0.8}, {"height", 0.5}, {"material", int64_t(1)}});
    name(sb, wheel, "Crank");
    name(sb, block, "Slider block");
    sb.connect("mechanism.motor", wheel, c, 0, c, {{"speed", 3.0}, {"maxTorque", 1e7}});
    sb.connect("connector.rod", wheel, {c.x + r, c.y}, block, {r + L, c.y});
    sb.connect("connector.slider", block, {r + L, c.y}, 0, {r + L + 1.0, c.y});
    view(sb, {2, 3}, 70);
}

void atwood(SceneBuilder& sb)
{
    ground(sb);
    const double side = 0.4;
    const ItemId a = sb.add("body.box", {-0.6, 2}, {{"width", side}, {"height", side}, {"density", 2.0 / (side * side)},
                                                    {"showForces", true}, {"label", QStringLiteral("2 kg")}});
    const ItemId b = sb.add("body.box", {0.6, 2}, {{"width", side}, {"height", side}, {"density", 1.0 / (side * side)},
                                                   {"showForces", true}, {"label", QStringLiteral("1 kg")},
                                                   {"color", Color::fromRgb(0x3daee9)}});
    name(sb, a, "Heavy (2 kg)");
    name(sb, b, "Light (1 kg)");
    const ItemId p = sb.connect("connector.pulley", a, {-0.6, 2}, b, {0.6, 2});
    sb.set(p, "groundA", Vec2{-0.6, 5});
    sb.set(p, "groundB", Vec2{0.6, 5});
    sb.set(p, "length", 6.0);
    view(sb, {0, 2.5}, 90);
}

void springMass(SceneBuilder& sb)
{
    const ItemId anchor = sb.add("body.box", {0, 6}, {{"width", 2}, {"height", 0.2}, {"bodyType", int64_t(0)},
                                                      {"color", Color::fromRgb(0x7f8c8d)}});
    name(sb, anchor, "Ceiling");
    const double side = 0.4;
    const ItemId m1 = sb.add("body.box", {-2, 3}, {{"width", side}, {"height", side}, {"density", 1.0 / (side * side)},
                                                   {"fixedRotation", true}, {"trail", true}});
    const ItemId m2 = sb.add("body.box", {2, 3}, {{"width", side}, {"height", side}, {"density", 1.0 / (side * side)},
                                                  {"fixedRotation", true}, {"trail", true},
                                                  {"color", Color::fromRgb(0x3daee9)}});
    name(sb, m1, "Undamped mass");
    name(sb, m2, "Damped mass");
    const ItemId s1 = sb.connect("connector.spring", 0, {-2, 6}, m1, {-2, 3.2}, {{"stiffness", 40.0}});
    const ItemId s2 = sb.connect("connector.spring", 0, {2, 6}, m2, {2, 3.2}, {{"stiffness", 40.0}, {"dampingRatio", 0.1}});
    sb.set(s1, "restLength", 2.0);
    sb.set(s2, "restLength", 2.0);
    view(sb, {0, 3.5}, 70);
}

void newtonsCradle(SceneBuilder& sb)
{
    // Classic one-in, one-out behaviour needs the collisions to happen one pair
    // at a time: a 5 mm gap and a 480 Hz physics rate (a ball moves less than
    // the gap per step) give an exact momentum hand-over.
    const double r = 0.25, L = 3.0, top = 5.0, gap = 0.005;
    for (int i = 0; i < 5; ++i) {
        const double x = (i - 2) * (2 * r + gap);
        Vec2 bob{x, top - L};
        if (i == 0) bob = {x - L * std::sin(0.6), top - L * std::cos(0.6)};
        const ItemId b = sb.add("body.circle", bob, {{"radius", r}, {"material", int64_t(1)}, {"restitution", 1.0},
                                                     {"staticFriction", 0.0}, {"kineticFriction", 0.0}});
        name(sb, b, "Ball");
        sb.connect("connector.rod", b, bob, 0, {x, top});
    }
    sb.setWorld("physicsHz", int64_t(480));
    sb.setWorld("keyframeInterval", int64_t(960));
    sb.setWorld("jointHertz", 240.0);
    sb.setWorld("restitutionThreshold", 0.01);
    sb.setWorld("sleep", false);
    view(sb, {0, 3}, 80);
}

void conveyors(SceneBuilder& sb)
{
    ground(sb);
    sb.add("mechanism.conveyor", {-3, 3}, {{"surfaceSpeed", 2.0}});
    sb.add("mechanism.conveyor", {3, 1.5}, {{"surfaceSpeed", -1.5}, {"angle", -0.1}});
    sb.add("mechanism.trampoline", {-3, 0.2}, {});
    for (int i = 0; i < 4; ++i) sb.add("body.box", {-5.2 + i * 0.7, 3.6}, {{"width", 0.5}, {"height", 0.5}});
    sb.add("body.circle", {-3, 6}, {{"radius", 0.3}, {"material", int64_t(3)}});
    view(sb, {0, 2.5}, 60);
}

void gears(SceneBuilder& sb)
{
    const ItemId g1 = sb.add("body.circle", {-1, 3}, {{"radius", 0.6}, {"layers", int64_t(1)}, {"mask", int64_t(1)},
                                                      {"color", Color::fromRgb(0xf39c1f)}});
    const ItemId g2 = sb.add("body.circle", {0.8, 3}, {{"radius", 1.2}, {"layers", int64_t(2)}, {"mask", int64_t(2)},
                                                       {"color", Color::fromRgb(0x3daee9)}});
    const ItemId g3 = sb.add("body.circle", {3.8, 3}, {{"radius", 0.6}, {"layers", int64_t(4)}, {"mask", int64_t(4)},
                                                       {"color", Color::fromRgb(0x27ae60)}});
    name(sb, g1, "Driver");
    name(sb, g2, "Idler");
    name(sb, g3, "Belt pulley");
    sb.connect("mechanism.motor", g1, {-1, 3}, 0, {-1, 3}, {{"speed", 2.0}, {"maxTorque", 1e6}});
    sb.connect("connector.hinge", g2, {0.8, 3}, 0, {0.8, 3});
    sb.connect("connector.hinge", g3, {3.8, 3}, 0, {3.8, 3});
    sb.connect("connector.gear", g1, {-1, 3}, g2, {0.8, 3}, {{"ratio", 2.0}});
    sb.connect("connector.gear", g2, {0.8, 3}, g3, {3.8, 3}, {{"ratio", 0.5}, {"direction", int64_t(1)}});
    view(sb, {1.4, 3}, 90);
}

void stressTest(SceneBuilder& sb)
{
    ground(sb, 80.0);
    sb.add("body.wall", {-30, 20}, {{"height", 40.0}});
    sb.add("body.wall", {30, 20}, {{"height", 40.0}});
    // 5,000 dynamic bodies: alternating boxes and circles in a staggered grid.
    const int cols = 100, rows = 50;
    const double s = 0.5;
    int n = 0;
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i, ++n) {
            const Vec2 p{-24.75 + i * s + (j % 2) * 0.1, 1.0 + j * s * 1.05};
            const uint32_t hue[] = {0x3daee9, 0xf39c1f, 0x27ae60, 0x9b59b6, 0xe74c3c};
            if ((i + j) % 2)
                sb.add("body.circle", p, {{"radius", 0.22}, {"color", Color::fromRgb(hue[n % 5])}});
            else
                sb.add("body.box", p, {{"width", 0.4}, {"height", 0.4}, {"color", Color::fromRgb(hue[n % 5])}});
        }
    }
    view(sb, {0, 14}, 22);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: kinetic-make-examples <output-dir>\n");
        return 2;
    }
    const QDir out(QString::fromLocal8Bit(argv[1]));
    QDir().mkpath(out.path());
    const std::vector<std::pair<const char*, std::function<void(SceneBuilder&)>>> scenes = {
        {"01_pendulum", pendulum},
        {"02_double_pendulum", doublePendulum},
        {"05_newtons_cradle", newtonsCradle},
        {"06_spring_mass_system", springMass},
        {"11_crank_slider", crankSlider},
        {"12_atwood_machine", atwood},
        {"13_gear_train", gears},
        {"14_conveyors_and_trampoline", conveyors},
        {"99_stress_test_5000_bodies", stressTest},
    };
    for (const auto& [file, build] : scenes) {
        SceneBuilder sb;
        build(sb);
        QFile f(out.filePath(QString::fromLatin1(file) + QStringLiteral(".json")));
        if (!f.open(QIODevice::WriteOnly) || f.write(sceneToBytes(sb.scene)) < 0) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(f.fileName()));
            return 1;
        }
        std::printf("wrote %s (%zu items)\n", qPrintable(f.fileName()), sb.scene.items.size());
    }
    return 0;
}
