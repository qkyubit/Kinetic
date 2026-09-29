// Documentation screenshot generator: drives the real UI through scripted
// scenarios and saves PNGs. Usage: kinetic-screenshots <output-dir> [scenario...]
#include "app/Document.h"
#include "app/MainWindow.h"
#include "app/Viewport.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTest>

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
        {"running",
         [](MainWindow& w) {
             editorScene(w);
             w.simulation()->play();
             settle(1500);
             w.simulation()->pause();
         }},
    };

    for (const Scenario& s : scenarios) {
        if (!wanted.isEmpty() && !wanted.contains(QLatin1String(s.name))) continue;
        MainWindow w;
        w.resize(1400, 860);
        w.show();
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
