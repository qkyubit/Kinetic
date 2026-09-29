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

void projectile(SceneBuilder& sb)
{
    ground(sb, 80.0);
    sb.setWorld("airDrag", true);
    // Same launch; only the drag coefficient differs.
    const Vec2 v0{14, 14};
    const ItemId a = sb.add("body.circle", {-18, 0.3}, {{"radius", 0.25}, {"density", 30.0}, {"velocity", v0},
                                                       {"dragCoefficient", 0.0}, {"trail", true}, {"trailLength", 10.0},
                                                       {"mask", int64_t(0xFFFD)},
                                                       {"color", Color::fromRgb(0xe74c3c)}, {"trailColor", Color::fromRgb(0xe74c3c)}});
    const ItemId b = sb.add("body.circle", {-18, 0.3}, {{"radius", 0.25}, {"density", 30.0}, {"velocity", v0},
                                                       {"dragCoefficient", 1.0}, {"trail", true}, {"trailLength", 10.0},
                                                       {"mask", int64_t(0xFFFD)}, {"layers", int64_t(2)},
                                                       {"color", Color::fromRgb(0x3daee9)}, {"trailColor", Color::fromRgb(0x3daee9)}});
    name(sb, a, "No drag (Cd = 0)");
    name(sb, b, "With drag (Cd = 1)");
    sb.set(a, "label", QStringLiteral("Cd = 0"));
    sb.set(b, "label", QStringLiteral("Cd = 1"));
    view(sb, {2, 5}, 19);
}

// Open-topped tank: floor and walls around a liquid region.
ItemId tank(SceneBuilder& sb, Vec2 bottomCentre, double w, double depth, int liquid, const char* n)
{
    const double wall = 0.2;
    sb.add("body.box", bottomCentre - Vec2{0, wall / 2}, {{"width", w + 2 * wall}, {"height", wall}, {"bodyType", int64_t(0)},
                                                         {"color", Color::fromRgb(0x7f8c8d)}});
    for (const double sx : {-1.0, 1.0})
        sb.add("body.box", bottomCentre + Vec2{sx * (w / 2 + wall / 2), (depth + 0.6) / 2},
               {{"width", wall}, {"height", depth + 0.6}, {"bodyType", int64_t(0)}, {"color", Color::fromRgb(0x7f8c8d)}});
    const ItemId f = sb.add("fluid.region", bottomCentre + Vec2{0, depth / 2}, {{"width", w}, {"height", depth}, {"liquid", int64_t(liquid)}});
    name(sb, f, n);
    return f;
}

void buoyancy(SceneBuilder& sb)
{
    ground(sb, 60.0, -0.25);
    sb.setWorld("sleep", false);
    tank(sb, {-5, 0}, 7.0, 2.5, 1, "Water");
    tank(sb, {4.5, 0}, 5.0, 2.5, 4, "Mercury");
    // Materials: density ratio decides the draft.
    struct Block {
        const char* name;
        double density;
        uint32_t color;
    };
    const Block water[] = {{"Pine (500)", 500, 0xe0b46c}, {"Ice (917)", 917, 0xd6eaf8}, {"Rubber (1100)", 1100, 0x2d3436}, {"Oak (750)", 750, 0xa0522d}};
    for (int i = 0; i < 4; ++i) {
        const ItemId b = sb.add("body.box", {-7.6 + i * 1.7, 3.6}, {{"width", 1.0}, {"height", 0.8}, {"density", water[i].density},
                                                                 {"color", Color::fromRgb(water[i].color)}});
        name(sb, b, water[i].name);
        sb.set(b, "label", QString::fromUtf8(water[i].name));
    }
    const Block mercury[] = {{"Steel (7850)", 7850, 0x95a5a6}, {"Lead (11340)", 11340, 0x636e72}, {"Gold (19300)", 19300, 0xf1c40f}};
    for (int i = 0; i < 3; ++i) {
        const ItemId b = sb.add("body.box", {3.0 + i * 1.5, 3.6}, {{"width", 0.9}, {"height", 0.7}, {"density", mercury[i].density},
                                                                {"color", Color::fromRgb(mercury[i].color)}});
        name(sb, b, mercury[i].name);
        sb.set(b, "label", QString::fromUtf8(mercury[i].name));
    }
    const ItemId ball = sb.add("body.circle", {-2.5, 4.5}, {{"radius", 0.4}, {"density", 240.0}, {"color", Color::fromRgb(0xc8a165)}});
    name(sb, ball, "Cork ball (240)");
    view(sb, {-0.75, 2}, 46);
}

void opticsBench(SceneBuilder& sb)
{
    sb.setWorld("gravity", Vec2{0, 0});
    // White beam into a dense-flint prism at 65° incidence, spectrum on a screen.
    const ItemId white = sb.add("optics.source", {-9, 0.9}, {{"white", true}, {"mode", int64_t(0)}, {"samples", int64_t(16)},
                                                            {"angle", 35.0 * kRadPerDeg}});
    name(sb, white, "White light");
    sb.add("optics.prism", {-5, 3.4}, {{"side", 1.8}, {"glass", int64_t(3)}});
    sb.add("optics.absorber", {-2.0, 1.2}, {{"length", 2.5}, {"angle", -35.0 * kRadPerDeg}});
    // Parallel beam focused by a glass lens (spherical aberration visible at the rim).
    const ItemId beam = sb.add("optics.source", {-9, -2}, {{"mode", int64_t(1)}, {"rays", int64_t(11)}, {"beamWidth", 1.4}, {"wavelength", 532.0}});
    name(sb, beam, "Green beam");
    const ItemId lens = sb.add("optics.lens", {-5, -2}, {{"radius1", 1.6}, {"radius2", -1.6}, {"thickness", 0.3}, {"aperture", 1.8}});
    name(sb, lens, "Biconvex lens");
    // Red laser bouncing inside a glass block (total internal reflection) after a mirror.
    const ItemId laser = sb.add("optics.source", {1.5, -3.8}, {{"mode", int64_t(0)}, {"wavelength", 650.0}, {"angle", 90.0 * kRadPerDeg}});
    name(sb, laser, "Red laser");
    sb.add("optics.mirror", {1.5, -1.5}, {{"angle", -45.0 * kRadPerDeg}, {"length", 0.8}});
    sb.add("optics.block", {5.0, -1.5}, {{"width", 3.0}, {"height", 0.8}, {"angle", -12.0 * kRadPerDeg}});
    // Point source at the focus of a concave mirror: a collimated beam.
    const ItemId bulb = sb.add("optics.source", {5.0, 3.0}, {{"mode", int64_t(2)}, {"rays", int64_t(15)}, {"spread", 70.0 * kRadPerDeg},
                                                          {"wavelength", 590.0}, {"angle", 180.0 * kRadPerDeg}});
    name(sb, bulb, "Point source");
    sb.add("optics.curvedMirror", {3.0, 3.0}, {{"radius", 4.0}, {"aperture", 2.6}});
    view(sb, {-1.5, 0.5}, 48);
}

void orbit(SceneBuilder& sb)
{
    sb.setWorld("gravity", Vec2{0, 0});
    sb.setWorld("mutualGravity", true);
    sb.setWorld("gravitationalConstant", 1.0);
    sb.setWorld("coulombSoftening", 0.01);
    sb.setWorld("sleep", false);
    const ItemId sun = sb.add("body.circle", {0, 0}, {{"radius", 0.4}, {"density", 3000.0}, {"color", Color::fromRgb(0xf1c40f)}});
    const ItemId planet = sb.add("body.circle", {5, 0}, {{"radius", 0.12}, {"density", 500.0}, {"color", Color::fromRgb(0x3daee9)},
                                                         {"trail", true}, {"trailLength", 30.0}});
    name(sb, sun, "Star");
    name(sb, planet, "Planet");
    const double M = sb.get<double>(sun, "mass"), m = sb.get<double>(planet, "mass");
    const double r = 5.0, mu = M + m;
    // Slightly elliptical orbit, zero total momentum.
    const double v = 0.85 * std::sqrt(mu / r);
    sb.set(planet, "velocity", Vec2{0, v * M / (M + m)});
    sb.set(sun, "velocity", Vec2{0, -v * m / (M + m)});
    view(sb, {1, 0}, 65);
}

void chargedParticles(SceneBuilder& sb)
{
    sb.setWorld("gravity", Vec2{0, 0});
    sb.setWorld("sleep", false);
    // Light particles (31 g) with µC charges: r = m v / (|q| B) = 1.5 m.
    const ItemId b = sb.add("field.magnetic", {-4, 0}, {{"width", 9.0}, {"height", 9.0}, {"strength", 3e4}});
    name(sb, b, "Magnetic field");
    struct P {
        double q, speed;
        Vec2 at;
        uint32_t color;
    };
    const P ps[] = {{1e-6, 1.43, {-5.5, 0.5}, 0xe74c3c}, {-1e-6, 1.43, {-2.5, -0.5}, 0x3daee9}, {5e-7, 0.95, {-4, 3.2}, 0xf39c1f}};
    for (const P& pp : ps) {
        const ItemId p = sb.add("body.circle", pp.at, {{"radius", 0.1}, {"density", 1.0}, {"charge", pp.q},
                                                      {"velocity", Vec2{pp.speed, 0}}, {"trail", true}, {"trailLength", 20.0},
                                                      {"mask", int64_t(0)}, {"color", Color::fromRgb(pp.color)},
                                                      {"trailColor", Color::fromRgb(pp.color)}});
        name(sb, p, pp.q > 0 ? "Positive charge" : "Negative charge");
    }
    // A fixed charge and an opposite charge orbiting it.
    const ItemId fixed = sb.add("body.circle", {9, 0}, {{"radius", 0.2}, {"charge", 1e-5}, {"bodyType", int64_t(0)}});
    name(sb, fixed, "Fixed +10 µC");
    const ItemId orbiter = sb.add("body.circle", {9, 1.5}, {{"radius", 0.1}, {"density", 1.0}, {"charge", -1e-6}, {"trail", true},
                                                           {"trailLength", 20.0}});
    name(sb, orbiter, "Orbiting −1 µC");
    const double m = sb.get<double>(orbiter, "mass");
    const double k = 8.9875517923e9 * 1e-5 * 1e-6;
    sb.set(orbiter, "velocity", Vec2{std::sqrt(k / (m * 1.5)), 0});
    view(sb, {2, 0}, 38);
}

void fields(SceneBuilder& sb)
{
    ground(sb, 60.0);
    sb.add("body.wall", {-14, 5}, {{"height", 11.0}});
    // A fan blowing balls up a ramp, a low-gravity zone and an explosion in a pile.
    const ItemId fan = sb.add("field.wind", {-10, 1.5}, {{"speed", 25.0}, {"fan", true}, {"width", 7.0}, {"height", 3.0},
                                                        {"angle", 20.0 * kRadPerDeg}});
    name(sb, fan, "Fan");
    for (int i = 0; i < 5; ++i) sb.add("body.circle", {-12.5 + i * 0.5, 0.3}, {{"radius", 0.2}, {"density", 30.0}});
    const ItemId moon = sb.add("field.gravity", {-1, 4}, {{"width", 5.0}, {"height", 8.0}});
    name(sb, moon, "Moon gravity");
    for (int i = 0; i < 4; ++i) sb.add("body.box", {-2.2 + i * 0.8, 0.5 + (i % 2) * 2.0}, {{"width", 0.5}, {"height", 0.5}, {"velocity", Vec2{0, 3.0}}});
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5 - j; ++i) sb.add("body.box", {5.0 + i * 0.62 + j * 0.31, 0.3 + j * 0.6}, {{"width", 0.6}, {"height", 0.6}});
    const ItemId boom = sb.add("field.explosion", {6.25, -0.1}, {{"time", 2.0}, {"radius", 4.0}, {"impulse", 900.0}});
    name(sb, boom, "Explosion at 2 s");
    // Two bar magnets snapping together.
    sb.add("body.magnet", {10.5, 0.2}, {{"staticFriction", 0.2}});
    sb.add("body.magnet", {12.0, 0.2}, {{"angle", 180.0 * kRadPerDeg}});
    const ItemId rocket = sb.add("body.box", {-6, 6}, {{"width", 0.4}, {"height", 1.2}, {"density", 200.0}, {"color", Color::fromRgb(0xdfe6e9)}});
    name(sb, rocket, "Rocket");
    sb.connect("mechanism.thruster", rocket, {-6, 5.4}, 0, {-6, 8}, {{"thrust", 1200.0}});
    view(sb, {-0.5, 4}, 28);
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
        {"03_projectile_with_and_without_drag", projectile},
        {"05_newtons_cradle", newtonsCradle},
        {"06_spring_mass_system", springMass},
        {"07_buoyancy_comparison", buoyancy},
        {"08_lens_and_prism_optics", opticsBench},
        {"09_two_body_orbit", orbit},
        {"10_charged_particles", chargedParticles},
        {"11_crank_slider", crankSlider},
        {"12_atwood_machine", atwood},
        {"13_gear_train", gears},
        {"14_conveyors_and_trampoline", conveyors},
        {"15_fields_fans_and_explosions", fields},
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
