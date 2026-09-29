#include "Benchmark.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>

#ifndef KINETIC_VERSION
#define KINETIC_VERSION "dev"
#endif

int main(int argc, char** argv)
{
    // Headless benchmark: no GUI, no display connection.
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--benchmark") == 0) return kn::runBenchmark(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kinetic"));
    QApplication::setOrganizationName(QStringLiteral("Kinetic"));
    QApplication::setApplicationVersion(QStringLiteral(KINETIC_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.kinetic.Kinetic"));
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName(QStringLiteral("breeze"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Kinetic, a 2D physics sandbox.\n\n"
                                                    "Headless benchmark: kinetic --benchmark <scene> --steps N"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("scene"), QStringLiteral("Scene file to open."));
    const QCommandLineOption example(QStringLiteral("example"), QStringLiteral("Open a bundled example by name."),
                                     QStringLiteral("name"));
    const QCommandLineOption run(QStringLiteral("run"), QStringLiteral("Start the simulation for <seconds> after start-up."),
                                 QStringLiteral("seconds"));
    const QCommandLineOption shot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a screenshot of the window to <file> and quit (documentation)."),
                                  QStringLiteral("file"));
    parser.addOptions({example, run, shot});
    parser.process(app);

    kn::MainWindow w;
    w.show();
    if (!parser.positionalArguments().isEmpty()) w.openFile(parser.positionalArguments().first());
    if (parser.isSet(example) && !w.openExample(parser.value(example)))
        qWarning("no bundled example matches '%s'", qPrintable(parser.value(example)));

    const double runSeconds = parser.value(run).toDouble();
    if (runSeconds > 0) {
        QTimer::singleShot(300, &w, [&w] { w.simulation()->play(); });
        QTimer::singleShot(300 + int(runSeconds * 1000), &w, [&w] { w.simulation()->pause(); });
    }
    if (parser.isSet(shot)) {
        const QString path = parser.value(shot);
        QTimer::singleShot(900 + int(std::max(0.0, runSeconds) * 1000), &w, [&w, path] {
            if (!w.screenshot().save(path)) qWarning("could not write %s", qPrintable(path));
            w.document()->undoStack()->setClean();
            QApplication::quit();
        });
    }
    return app.exec();
}
