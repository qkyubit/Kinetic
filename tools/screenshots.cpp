// Documentation screenshot generator: drives the real UI through scripted
// scenarios and saves PNGs. Usage: kinetic-screenshots <output-dir> [scenario...]
#include "app/Document.h"
#include "app/MainWindow.h"
#include "app/Viewport.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTest>

#include <cstdio>

using namespace kn;

namespace {

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QTest::qWait(10);
}

ItemDef make(Document& doc, const char* type, Vec2 at)
{
    return ItemRegistry::instance().find(QString::fromLatin1(type))->makeDefault(doc.allocateId(), at);
}

void editorScene(MainWindow& w)
{
    Document& doc = *w.document();
    std::vector<ItemDef> items;
    items.push_back(make(doc, "body.ground", {0, -0.5}));
    for (int i = 0; i < 4; ++i) items.push_back(make(doc, "body.box", {0, 0.5 + i}));
    items.push_back(make(doc, "body.circle", {3, 3}));
    items.push_back(make(doc, "body.capsule", {-3, 2}));
    items.push_back(make(doc, "body.polygon", {-5.5, 1}));
    items.push_back(make(doc, "body.freehand", {5, 1}));
    doc.addItems(items, "scene");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kinetic"));
    QApplication::setOrganizationName(QStringLiteral("KineticScreenshots"));
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        qWarning("usage: kinetic-screenshots <output-dir> [scenario...]");
        return 2;
    }
    const QDir out(args[0]);
    QDir().mkpath(out.path());
    QStringList wanted = args.mid(1);

    struct Scenario {
        const char* name;
        std::function<void(MainWindow&)> run;
    };
    const std::vector<Scenario> scenarios = {
        {"editor",
         [](MainWindow& w) {
             editorScene(w);
             w.document()->setSelection({w.document()->scene().items[3].id});
         }},
        {"gizmo-rotate",
         [](MainWindow& w) {
             editorScene(w);
             w.document()->setSelection({w.document()->scene().items[6].id});
             w.viewport()->setGizmo(Viewport::Gizmo::Rotate);
         }},
        {"gizmo-scale",
         [](MainWindow& w) {
             editorScene(w);
             w.document()->setSelection({w.document()->scene().items[6].id});
             w.viewport()->setGizmo(Viewport::Gizmo::Scale);
         }},
        {"gizmo-resize",
         [](MainWindow& w) {
             editorScene(w);
             w.document()->setSelection({w.document()->scene().items[6].id});
             w.viewport()->setGizmo(Viewport::Gizmo::Resize);
         }},
        {"measure",
         [](MainWindow& w) {
             editorScene(w);
             w.document()->setSelection({});
             Viewport* v = w.viewport();
             auto click = [v](Vec2 at, bool drag = false, Vec2 to = {}) {
                 const QPoint a = v->toScreen(at).toPoint();
                 QTest::mousePress(v, Qt::LeftButton, {}, a);
                 if (drag) {
                     const QPointF b = v->toScreen(to);
                     QMouseEvent move(QEvent::MouseMove, b, v->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, {});
                     QApplication::sendEvent(v, &move);
                     QTest::mouseRelease(v, Qt::LeftButton, {}, b.toPoint());
                 } else {
                     QTest::mouseRelease(v, Qt::LeftButton, {}, a);
                 }
             };
             const auto& items = w.document()->scene().items;
             const Aabb a = items[3].type->bounds(items[3], *v), b = items[6].type->bounds(items[6], *v);
             v->setTool(Viewport::Tool::Ruler);
             click(a.hi, true, b.center());
             v->setTool(Viewport::Tool::Protractor);
             click(b.center());
             click({b.hi.x, b.center().y});
             click(a.hi);
             // Leave the cursor over a corner to show the snap marker.
             const QPointF hover = v->toScreen({a.lo.x, a.hi.y});
             QMouseEvent move(QEvent::MouseMove, hover, v->mapToGlobal(hover), Qt::NoButton, Qt::NoButton, {});
             QApplication::sendEvent(v, &move);
         }},
        {"stress",
         [](MainWindow& w) {
             w.openExample(QStringLiteral("stress"));
             w.viewport()->setStatsVisible(true);
             w.simulation()->play();
             settle(1000);
             const uint64_t f0 = w.viewport()->frames();
             QElapsedTimer t;
             t.start();
             uint64_t maxAllocs = 0;
             while (t.elapsed() < 4000) {
                 QTest::qWait(5);
                 if (w.viewport()->frames() > f0 + 5) maxAllocs = std::max(maxAllocs, w.viewport()->lastRenderAllocations());
             }
             const double fps = double(w.viewport()->frames() - f0) / (double(t.elapsed()) / 1000.0);
             std::printf("stress: %.1f FPS over 4 s, render %.2f ms/frame, max allocations/frame %llu, physics %.2f ms/step, "
                         "t=%.2f s\n",
                         fps, w.viewport()->renderMs(), (unsigned long long)maxAllocs,
                         double(w.simulation()->snapshot().stepMs), w.simulation()->snapshot().time);
             w.simulation()->pause();
         }},
        {"running",
         [](MainWindow& w) {
             editorScene(w);
             w.simulation()->play();
             settle(1500);
             w.simulation()->pause();
         }},
    };

    // "example:<name>[@seconds]" opens a bundled example and runs it.
    std::vector<Scenario> all = scenarios;
    std::vector<QByteArray> names;
    for (const QString& wantedName : wanted) {
        if (!wantedName.startsWith(QLatin1String("example:"))) continue;
        names.push_back(wantedName.toUtf8());
    }
    for (const QByteArray& n : names) {
        all.push_back({n.constData(), [n](MainWindow& w) {
                           const QString spec = QString::fromUtf8(n).mid(8);
                           const double seconds = spec.contains(QLatin1Char('@')) ? spec.section(QLatin1Char('@'), 1).toDouble() : 2.0;
                           w.openExample(spec.section(QLatin1Char('@'), 0, 0));
                           if (seconds > 0) {
                               w.simulation()->play();
                               settle(int(seconds * 1000));
                               w.simulation()->pause();
                           }
                       }});
    }
    for (const Scenario& s : all) {
        if (!wanted.isEmpty() && !wanted.contains(QLatin1String(s.name))) continue;
        MainWindow w;
        w.resize(1400, 860);
        w.show();
        w.showStartPage(false);
        settle(300);
        s.run(w);
        settle(500);
        const QString path = out.filePath(QStringLiteral("%1.png").arg(QLatin1String(s.name)));
        if (!w.screenshot().save(path)) qWarning("cannot write %s", qPrintable(path));
        w.document()->undoStack()->setClean();
        w.simulation()->reset();
        w.close();
    }
    return 0;
}
