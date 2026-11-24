#include "Benchmark.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>

#include <cstdio>

#ifndef KINETIC_VERSION
#define KINETIC_VERSION "dev"
#endif

namespace {
QElapsedTimer startup;
}

int main(int argc, char** argv)
{
    startup.start();
    // Headless benchmark: no GUI, no display connection.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--benchmark") == 0) return kn::runBenchmark(argc, argv);
        if (qstrcmp(argv[i], "--schema") == 0) return kn::printSchemaReference();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kinetic"));
    QApplication::setOrganizationName(QStringLiteral("Kinetic"));
    QApplication::setApplicationVersion(QStringLiteral(KINETIC_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.qkyubit.Kinetic"));
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
    // Start-up profiling: print the time to the first frame and quit.
    if (qEnvironmentVariableIsSet("KINETIC_STARTUP_EXIT")) {
        QTimer::singleShot(0, &w, [] {
            std::printf("first frame after %lld ms in main()\n", startup.elapsed());
            QApplication::quit();
        });
    }
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
