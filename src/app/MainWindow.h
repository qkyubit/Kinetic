#pragma once

#include "Document.h"
#include "SimController.h"

#include <QMainWindow>

class QLabel;
class QMenu;
class QActionGroup;
class QStackedWidget;

namespace kn {

class Viewport;
class ToolboxPanel;
class ExplorerPanel;
class PropertiesPanel;
class PlaybackBar;
class TriggerLogPanel;
class StartPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool openFile(const QString& path);
    void openScene(const QByteArray& json, const QString& title);
    Document* document() const { return doc_; }
    Viewport* viewport() const { return viewport_; }
    SimController* simulation() const { return sim_; }
    // Opens a bundled example by (partial) name. Returns false if none matches.
    bool openExample(const QString& name);
    // Renders the whole window, including the RHI viewport, into an image.
    QImage screenshot();
    // Shows the welcome page instead of the viewport (or the viewport again).
    void showStartPage(bool on);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void buildMenus();
    void buildDocks();
    void restoreLayout();
    bool maybeSave();
    bool save();
    bool saveAs();
    void newScene();
    void open();
    void copy();
    void paste();
    void duplicate();
    void freezeState();
    void updateTitle();
    void showContextMenu(QPoint global);
    void activateToolboxItem(const ItemType* t);
    void newGraph();
    void saveReplay();
    void openReplay();

    Document* doc_;
    SimController* sim_;
    Viewport* viewport_;
    ToolboxPanel* toolbox_;
    ExplorerPanel* explorer_;
    PropertiesPanel* properties_;
    PlaybackBar* playback_;
    TriggerLogPanel* triggerLog_;
    StartPage* startPage_;
    QStackedWidget* stack_;
    int graphCount_ = 0;
    QByteArray defaultState_;

    QLabel* cursorLabel_;
    QLabel* zoomLabel_;
    QLabel* selectionLabel_;

    QAction *newAct_, *openAct_, *saveAct_, *saveAsAct_, *quitAct_;
    QAction *undoAct_, *redoAct_, *cutAct_, *copyAct_, *pasteAct_, *duplicateAct_, *deleteAct_, *selectAllAct_;
    QAction *groupAct_, *ungroupAct_, *freezeAct_;
    QAction *selectToolAct_, *moveAct_, *rotateAct_, *scaleAct_, *rulerAct_, *protractorAct_, *drawAct_;
    QAction *snapAct_, *gridAct_, *zoomFitAct_, *resetLayoutAct_, *statsAct_;
    QAction *probeAct_, *graphAct_, *saveReplayAct_, *openReplayAct_;
    QMenu* viewMenu_;
    QMenu* examplesMenu_;
};

} // namespace kn
