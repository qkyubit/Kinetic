#include "Benchmark.h"

#include "core/AllocCounter.h"
#include "core/Scene.h"
#include "core/Sim.h"
#include "core/TaskPool.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <chrono>
#include <cstdio>

namespace kn {

static QString resolveScene(const QString& arg)
{
    if (QFile::exists(arg)) return arg;
    // "stress" or "example:stress" -> first bundled example whose name contains it.
    QString key = arg;
    if (key.startsWith(QLatin1String("example:"))) key = key.mid(8);
    for (const QString& f : QDir(QStringLiteral(":/examples")).entryList({QStringLiteral("*.json")}))
        if (f.contains(key, Qt::CaseInsensitive)) return QStringLiteral(":/examples/") + f;
    return arg;
}

int runBenchmark(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addOption({QStringLiteral("benchmark"), QStringLiteral("Scene file or example name."), QStringLiteral("scene")});
    p.addOption({QStringLiteral("steps"), QStringLiteral("Physics steps to time."), QStringLiteral("N"), QStringLiteral("1200")});
    p.addOption({QStringLiteral("warmup"), QStringLiteral("Untimed steps first."), QStringLiteral("W"), QStringLiteral("120")});
    p.addOption({QStringLiteral("record"), QStringLiteral("Enable replay recording (keyframes).")});
    p.addOption({QStringLiteral("threads"), QStringLiteral("Physics worker threads (0 = automatic)."), QStringLiteral("N"), QStringLiteral("0")});
    p.addHelpOption();
    p.process(app);

    const QString path = resolveScene(p.value(QStringLiteral("benchmark")));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "kinetic: cannot open scene '%s'\n", qPrintable(path));
        return 2;
    }
    const SceneLoadResult r = sceneFromBytes(f.readAll());
    if (!r.ok) {
        std::fprintf(stderr, "kinetic: %s: %s\n", qPrintable(path), qPrintable(r.error));
        return 2;
    }
    TaskPool::configure(p.value(QStringLiteral("threads")).toInt());
    const long steps = std::max(1L, p.value(QStringLiteral("steps")).toLong());
    const long warmup = std::max(0L, p.value(QStringLiteral("warmup")).toLong());

    SimStart st;
    for (const ItemDef& d : r.scene.items) st.items.push_back(std::make_shared<ItemDef>(d));
    st.world = std::make_shared<ItemDef>(r.scene.world);
    st.recording = p.isSet(QStringLiteral("record"));
    SimCore core;
    core.start(st);
    for (long i = 0; i < warmup; ++i) core.advance();

    Snapshot snap;
    core.writeSnapshot(snap); // size the snapshot buffers once, like the physics thread does
    const WorldSettings& ws = core.world()->settings();
    const uint64_t interval = uint64_t(ws.keyframeInterval);

    std::vector<double> times;
    times.reserve(size_t(steps));
    uint64_t allocSteps = 0, allocKeyframes = 0, allocPublish = 0, allocBox2d = 0, stepsWithAllocs = 0;
    using Clock = std::chrono::steady_clock;
    for (long i = 0; i < steps; ++i) {
        const bool keyframeStep = st.recording && core.step() % interval == 0;
        const alloc::Scope scope;
        const auto t0 = Clock::now();
        core.advance();
        const auto t1 = Clock::now();
        if (keyframeStep) {
            allocKeyframes += scope.count();
        } else {
            const uint64_t n = core.world()->lastStepAllocations();
            allocSteps += n;
            allocBox2d += core.world()->lastStepBox2dAllocations();
            stepsWithAllocs += n > 0 ? 1 : 0;
        }
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        const alloc::Scope pub;
        core.writeSnapshot(snap);
        allocPublish += pub.count();
    }
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    for (double t : times) sum += t;
    const double mean = sum / double(times.size());
    auto pct = [&](double q) { return sorted[size_t(std::min<double>(double(sorted.size() - 1), q * double(sorted.size())))]; };

    std::printf("Kinetic benchmark\n");
    std::printf("scene:        %s (%zu items, %zu bodies)\n", qPrintable(QFileInfo(path).fileName()), r.scene.items.size(),
                snap.bodies.size());
    std::printf("steps:        %ld timed after %ld warm-up, %d Hz, %d sub-steps, %d worker threads, recording %s\n",
                steps, warmup, ws.hz, ws.substeps, TaskPool::instance()->workerCount(), st.recording ? "on" : "off");
    std::printf("ms per step:  mean %.3f  median %.3f  p95 %.3f  max %.3f\n", mean, pct(0.5), pct(0.95), sorted.back());
    std::printf("real time:    %.1fx faster than real time at %d Hz\n", (1000.0 / ws.hz) / mean, ws.hz);
    if (alloc::available()) {
        std::printf("allocations:  physics steps %llu total in %llu of %ld steps: %llu by Kinetic code, "
                    "%llu inside Box2D (pool growth, islands falling asleep/waking)\n",
                    (unsigned long long)allocSteps, (unsigned long long)stepsWithAllocs, steps,
                    (unsigned long long)(allocSteps - allocBox2d), (unsigned long long)allocBox2d);
        if (st.recording)
            std::printf("              keyframe steps %llu (capture + rebuild every %llu steps)\n",
                        (unsigned long long)allocKeyframes, (unsigned long long)interval);
        std::printf("              snapshot publish %llu\n", (unsigned long long)allocPublish);
    } else {
        std::printf("allocations:  counter unavailable (build without KINETIC_ALLOC_COUNTER)\n");
    }
    std::printf("energy:       total %.6g J, drift %.4f %%\n", snap.energy.total, snap.energy.driftPercent());
    return 0;
}

} // namespace kn

namespace kn {

int printSchemaReference()
{
    QTextStream out(stdout);
    auto typeName = [](PropType t) {
        switch (t) {
        case PropType::Double: return "number";
        case PropType::Int: return "integer";
        case PropType::Bool: return "bool";
        case PropType::Enum: return "choice (index)";
        case PropType::Vector: return "[x, y]";
        case PropType::Colour: return "colour \"#aarrggbb\"";
        case PropType::String: return "string";
        case PropType::Points: return "[[x, y], ...]";
        case PropType::ItemRef: return "item id";
        case PropType::Bits: return "bit mask";
        }
        return "";
    };
    auto value = [](const PropDef& p) -> QString {
        if (p.type == PropType::Enum) {
            QStringList o;
            for (int i = 0; i < p.options.size(); ++i) o << QStringLiteral("%1 %2").arg(i).arg(p.options[i]);
            return o.join(QStringLiteral(", "));
        }
        if (p.type == PropType::Double) {
            const double s = p.scale;
            return QStringLiteral("%1 … %2%3").arg(p.min * s).arg(p.max * s).arg(p.unit.isEmpty() ? QString() : QLatin1Char(' ') + p.unit);
        }
        if (p.type == PropType::Int) return QStringLiteral("%1 … %2").arg(p.min).arg(p.max);
        return p.unit;
    };
    for (const ItemType* t : ItemRegistry::instance().types()) {
        const ItemMeta& m = t->meta();
        out << "### " << (m.name.isEmpty() ? m.typeId : m.name) << " (`" << m.typeId << "`)\n\n";
        if (!m.description.isEmpty()) out << m.description << "\n\n";
        out << "| key | label | type | range / values |\n|---|---|---|---|\n";
        for (const PropDef& p : t->schema().defs()) {
            if (p.has(PF_Transient) || p.has(PF_Hidden)) continue;
            out << "| `" << p.key << "` | " << p.label << " | " << typeName(p.type) << " | " << value(p) << " |\n";
        }
        out << "\n";
    }
    return 0;
}

} // namespace kn
