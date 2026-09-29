// Welcome page shown in place of the viewport at start-up: new/open, recent
// scenes and the bundled examples.
#pragma once

#include <QWidget>

class QListWidget;
class QCheckBox;

namespace kn {

// "03_projectile_with_and_without_drag.json" -> "Projectile with and without drag".
QString exampleTitle(const QString& fileName);
QStringList exampleFiles();

// Recent scene files (most recent first), kept in QSettings.
QStringList recentFiles();
void addRecentFile(const QString& path);

class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget* parent = nullptr);
    void refresh();

signals:
    void newSceneRequested();
    void openRequested();
    void fileRequested(const QString& path);
    void exampleRequested(const QString& fileName);

private:
    QListWidget* recent_;
    QListWidget* examples_;
    QCheckBox* showAtStart_;
};

} // namespace kn
