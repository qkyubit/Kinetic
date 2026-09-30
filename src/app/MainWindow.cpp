#include "MainWindow.h"

#include "Analysis.h"
#include "AnalysisPanel.h"
#include "ExplorerPanel.h"
#include "GraphPanel.h"
#include "Icons.h"
#include "PlaybackBar.h"
#include "PhysicsOptionsDialog.h"
#include "PropertiesPanel.h"
#include "ConstantsPanel.h"
#include "StartPage.h"
#include "ToolboxPanel.h"
#include "TriggerLogPanel.h"
#include "Viewport.h"

#include "core/Replay.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>

namespace kn {

namespace {
constexpr const char* kClipboardMime = "application/x-kinetic-items";
const QString kFileFilter = QObject::tr("Kinetic scenes (*.kinetic *.json);;All files (*)");
} // namespace

MainWindow::MainWindow()
{
    setObjectName(QStringLiteral("KineticMainWindow"));
    setWindowIcon(QIcon(QStringLiteral(":/io.github.kinetic.Kinetic.svg")));
    doc_ = new Document(this);
    sim_ = new SimController(doc_, this);
    viewport_ = new Viewport(doc_, sim_, this);
    startPage_ = new StartPage(this);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(viewport_);
    stack_->addWidget(startPage_);
    setCentralWidget(stack_);
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);

    buildActions();
    buildDocks();
    buildMenus();

    cursorLabel_ = new QLabel(this);
    zoomLabel_ = new QLabel(this);
    selectionLabel_ = new QLabel(this);
    statusBar()->addWidget(cursorLabel_);
    statusBar()->addPermanentWidget(selectionLabel_);
    statusBar()->addPermanentWidget(zoomLabel_);
    connect(viewport_, &Viewport::cursorMoved, this, [this](Vec2 w) {
        cursorLabel_->setText(tr("x = %1 m   y = %2 m").arg(w.x, 0, 'f', 3).arg(w.y, 0, 'f', 3));
    });
    auto zoomText = [this] {
        zoomLabel_->setText(tr("%1 px/m   grid %2 m").arg(viewport_->pixelsPerMetre(), 0, 'f', 1).arg(viewport_->gridStep()));
    };
    connect(viewport_, &Viewport::viewChanged, this, zoomText);
    zoomText();
    connect(doc_, &Document::selectionChanged, this, [this] {
        const int n = int(doc_->selection().size());
        selectionLabel_->setText(n ? tr("%n selected", nullptr, n) : QString());
        for (QAction* a : {cutAct_, copyAct_, duplicateAct_, deleteAct_, groupAct_, ungroupAct_}) a->setEnabled(n > 0);
    });
    connect(viewport_, &Viewport::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(explorer_, &ExplorerPanel::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(toolbox_, &ToolboxPanel::activated, this, &MainWindow::activateToolboxItem);
    connect(doc_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    connect(viewport_, &Viewport::toolChanged, this, [this] {
        const auto t = viewport_->tool();
        selectToolAct_->setChecked(t == Viewport::Tool::Select);
        rulerAct_->setChecked(t == Viewport::Tool::Ruler);
        protractorAct_->setChecked(t == Viewport::Tool::Protractor);
        drawAct_->setChecked(t == Viewport::Tool::Draw);
        probeAct_->setChecked(t == Viewport::Tool::Probe);
        grabAct_->setChecked(t == Viewport::Tool::Grab);
        viewport_->update();
    });

    resize(1400, 900);
    defaultState_ = saveState();
    restoreLayout();
    doc_->reset(SceneData::empty());
    updateTitle();

    connect(startPage_, &StartPage::newSceneRequested, this, [this] {
        newScene();
        showStartPage(false);
    });
    connect(startPage_, &StartPage::openRequested, this, &MainWindow::open);
    connect(startPage_, &StartPage::fileRequested, this, &MainWindow::openFile);
    connect(startPage_, &StartPage::exampleRequested, this, [this](const QString& f) { openExample(f); });
    showStartPage(QSettings().value(QStringLiteral("startPage/show"), true).toBool());
}

void MainWindow::showStartPage(bool on)
{
    if (on) startPage_->refresh();
    stack_->setCurrentWidget(on ? static_cast<QWidget*>(startPage_) : viewport_);
    if (!on) viewport_->setFocus();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildActions()
{
    auto act = [this](const QString& text, const QString& icon, const QString& fallback, QKeySequence ks = {}) {
        auto* a = new QAction(themedIcon(icon, fallback), text, this);
        if (!ks.isEmpty()) a->setShortcut(ks);
        addAction(a);
        return a;
    };
    const QString fb = QStringLiteral(":/icons/tool.svg");
    newAct_ = act(tr("&New"), QStringLiteral("document-new"), fb, QKeySequence::New);
    openAct_ = act(tr("&Open…"), QStringLiteral("document-open"), fb, QKeySequence::Open);
    saveAct_ = act(tr("&Save"), QStringLiteral("document-save"), fb, QKeySequence::Save);
    saveAsAct_ = act(tr("Save &As…"), QStringLiteral("document-save-as"), fb, QKeySequence::SaveAs);
    quitAct_ = act(tr("&Quit"), QStringLiteral("application-exit"), fb, QKeySequence::Quit);
    connect(newAct_, &QAction::triggered, this, &MainWindow::newScene);
    connect(openAct_, &QAction::triggered, this, &MainWindow::open);
    connect(saveAct_, &QAction::triggered, this, &MainWindow::save);
    connect(saveAsAct_, &QAction::triggered, this, &MainWindow::saveAs);
    connect(quitAct_, &QAction::triggered, this, &QWidget::close);

    undoAct_ = doc_->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    undoAct_->setIcon(themedIcon(QStringLiteral("edit-undo"), fb));
    redoAct_ = doc_->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAct_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    redoAct_->setIcon(themedIcon(QStringLiteral("edit-redo"), fb));
    addAction(undoAct_);
    addAction(redoAct_);
    cutAct_ = act(tr("Cu&t"), QStringLiteral("edit-cut"), fb, QKeySequence::Cut);
    copyAct_ = act(tr("&Copy"), QStringLiteral("edit-copy"), fb, QKeySequence::Copy);
    pasteAct_ = act(tr("&Paste"), QStringLiteral("edit-paste"), fb, QKeySequence::Paste);
    duplicateAct_ = act(tr("D&uplicate"), QStringLiteral("edit-duplicate"), fb, QKeySequence(Qt::CTRL | Qt::Key_D));
    deleteAct_ = act(tr("&Delete"), QStringLiteral("edit-delete"), fb, QKeySequence::Delete);
    selectAllAct_ = act(tr("Select &All"), QStringLiteral("edit-select-all"), fb, QKeySequence::SelectAll);
    groupAct_ = act(tr("&Group"), QStringLiteral("object-group"), QStringLiteral(":/icons/group.svg"),
                    QKeySequence(Qt::CTRL | Qt::Key_G));
    ungroupAct_ = act(tr("U&ngroup"), QStringLiteral("object-ungroup"), QStringLiteral(":/icons/group.svg"),
                      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    freezeAct_ = act(tr("&Freeze Current State into Scene"), QStringLiteral("snapshot"), fb);
    freezeAct_->setToolTip(tr("Copy the simulated positions and velocities into the scene's initial state"));
    connect(cutAct_, &QAction::triggered, this, [this] {
        copy();
        doc_->deleteItems(doc_->selection());
    });
    connect(copyAct_, &QAction::triggered, this, &MainWindow::copy);
    connect(pasteAct_, &QAction::triggered, this, &MainWindow::paste);
    connect(duplicateAct_, &QAction::triggered, this, &MainWindow::duplicate);
    connect(deleteAct_, &QAction::triggered, this, [this] { doc_->deleteItems(doc_->selection()); });
    connect(selectAllAct_, &QAction::triggered, this, [this] {
        QList<ItemId> ids;
        for (const ItemDef& d : doc_->scene().items)
            if (!d.parent && !doc_->effectiveLocked(d)) ids << d.id;
        doc_->setSelection(ids);
    });
    connect(groupAct_, &QAction::triggered, this, [this] {
        if (!doc_->selection().isEmpty()) doc_->groupItems(doc_->selection());
    });
    connect(ungroupAct_, &QAction::triggered, this, [this] {
        for (ItemId id : doc_->selection()) doc_->ungroup(id);
    });
    connect(freezeAct_, &QAction::triggered, this, &MainWindow::freezeState);

    auto* tools = new QActionGroup(this);
    selectToolAct_ = act(tr("&Select"), QStringLiteral("edit-select"), fb, QKeySequence(Qt::Key_Q));
    // W, A, S and D are left free for panning the camera in the viewport.
    moveAct_ = act(tr("&Move Gizmo"), QStringLiteral("transform-move"), fb, QKeySequence(Qt::Key_T));
    rotateAct_ = act(tr("&Rotate Gizmo"), QStringLiteral("transform-rotate"), fb, QKeySequence(Qt::Key_E));
    scaleAct_ = act(tr("S&cale Gizmo"), QStringLiteral("transform-scale"), fb, QKeySequence(Qt::Key_R));
    drawAct_ = act(tr("&Draw Shape"), QStringLiteral("draw-freehand"), fb, QKeySequence(Qt::Key_B));
    rulerAct_ = act(tr("R&uler"), QStringLiteral("tool-measure"), fb, QKeySequence(Qt::Key_M));
    protractorAct_ = act(tr("&Protractor"), QStringLiteral("draw-halfcircle1"), fb, QKeySequence(Qt::Key_O));
    probeAct_ = act(tr("Pro&be"), QStringLiteral("crosshairs"), fb, QKeySequence(Qt::Key_P));
    probeAct_->setToolTip(tr("Hover over a body to read its velocity, acceleration and forces"));
    grabAct_ = act(tr("&Grab"), QStringLiteral("transform-browse"), fb, QKeySequence(Qt::Key_H));
    grabAct_->setToolTip(tr("Grab bodies while simulating: they keep their physics, swing, spin, push other bodies "
                            "and fly off when you let go (H)"));
    for (QAction* a : {selectToolAct_, drawAct_, rulerAct_, protractorAct_, probeAct_, grabAct_}) {
        a->setCheckable(true);
        tools->addAction(a);
    }
    selectToolAct_->setChecked(true);
    auto* gizmos = new QActionGroup(this);
    for (QAction* a : {moveAct_, rotateAct_, scaleAct_}) {
        a->setCheckable(true);
        gizmos->addAction(a);
    }
    moveAct_->setChecked(true);
    connect(selectToolAct_, &QAction::triggered, this, [this] { viewport_->setTool(Viewport::Tool::Select); });
    connect(drawAct_, &QAction::triggered, this,
            [this] { viewport_->beginDraw(ItemRegistry::instance().find(QStringLiteral("body.freehand"))); });
    connect(rulerAct_, &QAction::triggered, this, [this] { viewport_->setTool(Viewport::Tool::Ruler); });
    connect(protractorAct_, &QAction::triggered, this, [this] { viewport_->setTool(Viewport::Tool::Protractor); });
    connect(probeAct_, &QAction::triggered, this, [this] { viewport_->setTool(Viewport::Tool::Probe); });
    connect(grabAct_, &QAction::triggered, this, [this] {
        viewport_->setTool(Viewport::Tool::Grab);
        viewport_->setFocus();
    });
    connect(moveAct_, &QAction::triggered, this, [this] {
        viewport_->setTool(Viewport::Tool::Select);
        viewport_->setGizmo(Viewport::Gizmo::Move);
    });
    connect(rotateAct_, &QAction::triggered, this, [this] {
        viewport_->setTool(Viewport::Tool::Select);
        viewport_->setGizmo(Viewport::Gizmo::Rotate);
    });
    connect(scaleAct_, &QAction::triggered, this, [this] {
        viewport_->setTool(Viewport::Tool::Select);
        viewport_->setGizmo(Viewport::Gizmo::Scale);
    });

    snapAct_ = act(tr("S&nap to Grid"), QStringLiteral("snap-nodes-cusp"), fb, QKeySequence(Qt::Key_N));
    snapAct_->setCheckable(true);
    snapAct_->setChecked(viewport_->snapping());
    snapAct_->setToolTip(tr("Snap positions to the grid and rotations to 15° (hold Ctrl while dragging to invert)"));
    connect(snapAct_, &QAction::toggled, viewport_, &Viewport::setSnapping);
    gridAct_ = act(tr("Show &Grid"), QStringLiteral("view-grid"), fb, QKeySequence(Qt::Key_G));
    gridAct_->setCheckable(true);
    gridAct_->setChecked(true);
    connect(gridAct_, &QAction::toggled, viewport_, &Viewport::setGridVisible);
    zoomFitAct_ = act(tr("&Zoom to Fit"), QStringLiteral("zoom-fit-best"), fb, QKeySequence(Qt::Key_F));
    connect(zoomFitAct_, &QAction::triggered, viewport_, &Viewport::zoomToFit);
    statsAct_ = act(tr("&Performance Statistics"), QStringLiteral("office-chart-line"), fb, QKeySequence(Qt::Key_F12));
    statsAct_->setCheckable(true);
    statsAct_->setToolTip(tr("Frame rate, render and physics time, and heap allocations per frame/step"));
    connect(statsAct_, &QAction::toggled, viewport_, &Viewport::setStatsVisible);
    resetLayoutAct_ = act(tr("&Reset Layout"), QStringLiteral("view-restore"), fb);
    connect(resetLayoutAct_, &QAction::triggered, this, [this] { restoreState(defaultState_); });
    graphAct_ = act(tr("New &Graph"), QStringLiteral("office-chart-line"), fb, QKeySequence(Qt::CTRL | Qt::Key_K));
    graphAct_->setToolTip(tr("Open a graph of the selected items (or the scene's energy)"));
    connect(graphAct_, &QAction::triggered, this, &MainWindow::newGraph);
    saveReplayAct_ = act(tr("Save &Replay…"), QStringLiteral("media-record"), QStringLiteral(":/icons/record.svg"));
    saveReplayAct_->setToolTip(tr("Save the initial scene and every edit made while running, for exact playback"));
    connect(saveReplayAct_, &QAction::triggered, this, &MainWindow::saveReplay);
    openReplayAct_ = act(tr("Open Re&play…"), QStringLiteral("media-playback-start"), QStringLiteral(":/icons/play.svg"));
    connect(openReplayAct_, &QAction::triggered, this, &MainWindow::openReplay);

    // Toolbars.
    auto* main = addToolBar(tr("Main"));
    main->setObjectName(QStringLiteral("mainToolBar"));
    main->addActions({newAct_, openAct_, saveAct_});
    main->addSeparator();
    main->addActions({undoAct_, redoAct_});
    auto* toolBar = addToolBar(tr("Tools"));
    toolBar->setObjectName(QStringLiteral("toolsToolBar"));
    toolBar->addActions({selectToolAct_, grabAct_, moveAct_, rotateAct_, scaleAct_});
    toolBar->addSeparator();
    toolBar->addActions({drawAct_, rulerAct_, protractorAct_, probeAct_});
    toolBar->addSeparator();
    toolBar->addActions({snapAct_, gridAct_, zoomFitAct_});
    playback_ = new PlaybackBar(sim_, this);
    addToolBarBreak();
    addToolBar(Qt::TopToolBarArea, playback_);
}

void MainWindow::buildDocks()
{
    toolbox_ = new ToolboxPanel(this);
    explorer_ = new ExplorerPanel(doc_, this);
    properties_ = new PropertiesPanel(doc_, sim_, this);
    auto dock = [this](const QString& title, const QString& name, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        addDockWidget(area, d);
        return d;
    };
    auto* tb = dock(tr("Toolbox"), QStringLiteral("toolboxDock"), toolbox_, Qt::LeftDockWidgetArea);
    auto* ex = dock(tr("Explorer"), QStringLiteral("explorerDock"), explorer_, Qt::RightDockWidgetArea);
    auto* pr = dock(tr("Properties"), QStringLiteral("propertiesDock"), properties_, Qt::RightDockWidgetArea);
    splitDockWidget(ex, pr, Qt::Vertical);
    auto* an = dock(tr("Analysis"), QStringLiteral("analysisDock"), new AnalysisPanel(viewport_->analysis(), doc_, this),
                    Qt::RightDockWidgetArea);
    triggerLog_ = new TriggerLogPanel(doc_, sim_, this);
    auto* tl = dock(tr("Trigger Log"), QStringLiteral("triggerLogDock"), triggerLog_, Qt::BottomDockWidgetArea);
    auto* cp = dock(tr("Constants"), QStringLiteral("constantsDock"), new ConstantsPanel(doc_, this), Qt::RightDockWidgetArea);
    tabifyDockWidget(pr, an);
    tabifyDockWidget(an, cp);
    pr->raise();
    tl->hide();
    resizeDocks({tb}, {230}, Qt::Horizontal);
    resizeDocks({ex, pr}, {380, 380}, Qt::Horizontal);
    resizeDocks({ex, pr}, {250, 550}, Qt::Vertical);
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({newAct_, openAct_});
    examplesMenu_ = file->addMenu(themedIcon(QStringLiteral("folder-open"), QStringLiteral(":/icons/tool.svg")),
                                  tr("Open &Example"));
    const QStringList examples = exampleFiles();
    for (const QString& f : examples) {
        const QString title = exampleTitle(f);
        QAction* a = examplesMenu_->addAction(title);
        connect(a, &QAction::triggered, this, [this, f, title] {
            if (!maybeSave()) return;
            QFile in(QStringLiteral(":/examples/") + f);
            if (in.open(QIODevice::ReadOnly)) openScene(in.readAll(), title);
        });
    }
    examplesMenu_->setEnabled(!examples.isEmpty());
    file->addSeparator();
    file->addActions({saveAct_, saveAsAct_});
    file->addSeparator();
    file->addActions({openReplayAct_, saveReplayAct_});
    file->addSeparator();
    file->addAction(quitAct_);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({undoAct_, redoAct_});
    edit->addSeparator();
    edit->addActions({cutAct_, copyAct_, pasteAct_, duplicateAct_, deleteAct_});
    edit->addSeparator();
    edit->addActions({selectAllAct_, groupAct_, ungroupAct_});
    edit->addSeparator();
    edit->addAction(freezeAct_);

    viewMenu_ = menuBar()->addMenu(tr("&View"));
    for (QDockWidget* d : findChildren<QDockWidget*>()) viewMenu_->addAction(d->toggleViewAction());
    viewMenu_->addSeparator();
    viewMenu_->addActions({gridAct_, snapAct_, zoomFitAct_, statsAct_});
    viewMenu_->addSeparator();
    viewMenu_->addAction(graphAct_);
    viewMenu_->addSeparator();
    viewMenu_->addAction(resetLayoutAct_);

    QMenu* simMenu = menuBar()->addMenu(tr("&Simulation"));
    simMenu->addActions({playback_->playAction(), playback_->stepAction(), playback_->resetAction()});
    simMenu->addSeparator();
    simMenu->addActions({playback_->recordAction(), playback_->rewindAction()});

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addActions({selectToolAct_, grabAct_, moveAct_, rotateAct_, scaleAct_});
    tools->addSeparator();
    tools->addActions({drawAct_, rulerAct_, protractorAct_, probeAct_});

    QMenu* settings = menuBar()->addMenu(tr("S&ettings"));
    settings->addAction(themedIcon(QStringLiteral("configure"), QStringLiteral(":/icons/tool.svg")), tr("Configure &Physics…"),
                        this, [this] { PhysicsOptionsDialog(doc_, this).exec(); });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(themedIcon(QStringLiteral("go-home"), QStringLiteral(":/icons/tool.svg")), tr("&Start Page"), this,
                    [this] { showStartPage(true); });
    help->addSeparator();
    help->addAction(themedIcon(QStringLiteral("help-about"), QStringLiteral(":/icons/tool.svg")), tr("&About Kinetic"),
                    this, [this] {
                        QMessageBox::about(this, tr("About Kinetic"),
                                           tr("<h3>Kinetic %1</h3><p>A 2D physics sandbox.</p>"
                                              "<p>Rigid bodies by Box2D %2.%3.%4. Units are SI; the view scale is "
                                              "shown in pixels per metre.</p>")
                                               .arg(QApplication::applicationVersion())
                                               .arg(b2GetVersion().major)
                                               .arg(b2GetVersion().minor)
                                               .arg(b2GetVersion().revision));
                    });
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::restoreLayout()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    viewport_->setSnapping(s.value(QStringLiteral("view/snap"), true).toBool());
    snapAct_->setChecked(viewport_->snapping());
    gridAct_->setChecked(s.value(QStringLiteral("view/grid"), true).toBool());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("view/snap"), viewport_->snapping());
    s.setValue(QStringLiteral("view/grid"), viewport_->gridVisible());
    sim_->reset();
    e->accept();
}

void MainWindow::updateTitle()
{
    const QString name = doc_->filePath().isEmpty() ? tr("Untitled") : QFileInfo(doc_->filePath()).fileName();
    setWindowTitle(QStringLiteral("%1[*] — Kinetic").arg(name));
    setWindowModified(!doc_->undoStack()->isClean());
}

bool MainWindow::maybeSave()
{
    if (doc_->undoStack()->isClean()) return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes to the current scene?"),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

void MainWindow::newScene()
{
    if (!maybeSave()) return;
    doc_->setFilePath({});
    SceneData scene = SceneData::empty();
    PhysicsOptionsDialog::applyDefaults(scene.world);
    doc_->reset(std::move(scene));
    showStartPage(false);
    updateTitle();
}

void MainWindow::open()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Scene"), QString(), kFileFilter);
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::openScene(const QByteArray& json, const QString& title)
{
    SceneLoadResult r = sceneFromBytes(json);
    if (!r.ok) {
        QMessageBox::warning(this, tr("Cannot open scene"), tr("%1: %2").arg(title, r.error));
        return;
    }
    doc_->setFilePath({});
    doc_->reset(std::move(r.scene));
    showStartPage(false);
    if (!r.warnings.isEmpty())
        statusBar()->showMessage(tr("Loaded with %n warning(s): %1", nullptr, int(r.warnings.size())).arg(r.warnings.first()),
                                 10000);
    updateTitle();
}

bool MainWindow::openFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Cannot open scene"), tr("%1: %2").arg(path, f.errorString()));
        return false;
    }
    SceneLoadResult r = sceneFromBytes(f.readAll());
    if (!r.ok) {
        QMessageBox::warning(this, tr("Cannot open scene"), tr("%1: %2").arg(QFileInfo(path).fileName(), r.error));
        return false;
    }
    doc_->reset(std::move(r.scene));
    doc_->setFilePath(path);
    addRecentFile(path);
    showStartPage(false);
    if (r.fileVersion < kSceneSchemaVersion)
        statusBar()->showMessage(tr("Upgraded scene from schema version %1 to %2; saving will write the new format.")
                                     .arg(r.fileVersion)
                                     .arg(kSceneSchemaVersion),
                                 10000);
    if (!r.warnings.isEmpty()) {
        QMessageBox box(QMessageBox::Information, tr("Scene loaded with warnings"),
                        tr("%n problem(s) were found and repaired while loading.", nullptr, int(r.warnings.size())),
                        QMessageBox::Ok, this);
        box.setDetailedText(r.warnings.join(QLatin1Char('\n')));
        box.exec();
    }
    updateTitle();
    return true;
}

bool MainWindow::save()
{
    if (doc_->filePath().isEmpty()) return saveAs();
    QSaveFile f(doc_->filePath());
    if (!f.open(QIODevice::WriteOnly) || f.write(sceneToBytes(doc_->scene())) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Cannot save scene"), tr("%1: %2").arg(doc_->filePath(), f.errorString()));
        return false;
    }
    doc_->undoStack()->setClean();
    addRecentFile(doc_->filePath());
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(doc_->filePath()).fileName()), 3000);
    updateTitle();
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Scene"), doc_->filePath(), kFileFilter);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".kinetic");
    doc_->setFilePath(path);
    return save();
}

void MainWindow::copy()
{
    if (doc_->selection().isEmpty()) return;
    const QByteArray json = doc_->serializeItems(doc_->selection());
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(kClipboardMime), json);
    mime->setText(QString::fromUtf8(json));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void MainWindow::paste()
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return;
    QByteArray json = mime->data(QLatin1String(kClipboardMime));
    if (json.isEmpty()) json = mime->text().toUtf8();
    QString err;
    const double off = viewport_->gridStep() * 2;
    std::vector<ItemDef> items = doc_->deserializeItems(json, {off, -off}, &err);
    if (items.empty()) {
        if (!err.isEmpty()) statusBar()->showMessage(err, 4000);
        return;
    }
    doc_->addItems(std::move(items), tr("Paste"));
}

void MainWindow::duplicate()
{
    if (doc_->selection().isEmpty()) return;
    const double off = viewport_->gridStep() * 2;
    std::vector<ItemDef> items = doc_->deserializeItems(doc_->serializeItems(doc_->selection()), {off, -off}, nullptr);
    doc_->addItems(std::move(items), tr("Duplicate"));
}

void MainWindow::freezeState()
{
    if (!sim_->live()) {
        statusBar()->showMessage(tr("Run or step the simulation first."), 3000);
        return;
    }
    std::vector<ItemDef> after;
    for (const BodySnap& b : sim_->snapshot().bodies) {
        const ItemDef* cur = doc_->item(b.id);
        if (!cur) continue;
        ItemDef d = *cur;
        const auto& sch = d.type->schema();
        d.type->setPose(d, b.pose());
        const int vi = sch.indexOf(QStringLiteral("velocity"));
        const int wi = sch.indexOf(QStringLiteral("angularVelocity"));
        if (vi >= 0) d.set(vi, clampValue(sch[vi], Vec2{b.vx, b.vy}));
        if (wi >= 0) d.set(wi, clampValue(sch[wi], double(b.w)));
        after.push_back(std::move(d));
    }
    doc_->editItems(after, tr("Freeze state"));
}

void MainWindow::activateToolboxItem(const ItemType* t)
{
    if (t->meta().create == CreateMode::Draw) {
        viewport_->beginDraw(t);
        viewport_->setFocus();
        return;
    }
    viewport_->placeItem(t, viewport_->center());
    viewport_->setFocus();
}

void MainWindow::showContextMenu(QPoint global)
{
    QMenu m(this);
    m.addActions({cutAct_, copyAct_, pasteAct_, duplicateAct_, deleteAct_});
    m.addSeparator();
    m.addActions({groupAct_, ungroupAct_});
    m.addSeparator();
    m.addActions({graphAct_, zoomFitAct_});
    m.exec(global);
}

} // namespace kn

namespace kn {

bool MainWindow::openExample(const QString& name)
{
    for (const QString& f : exampleFiles()) {
        if (!f.contains(name, Qt::CaseInsensitive)) continue;
        if (!maybeSave()) return true;
        QFile in(QStringLiteral(":/examples/") + f);
        if (!in.open(QIODevice::ReadOnly)) return false;
        openScene(in.readAll(), exampleTitle(f));
        return true;
    }
    return false;
}

QImage MainWindow::screenshot()
{
    QImage img = grab().toImage();
    if (stack_->currentWidget() != viewport_) return img;
    const QImage vp = viewport_->grabFramebuffer();
    QPainter p(&img);
    const QRect r(viewport_->mapTo(this, QPoint(0, 0)), viewport_->size());
    p.drawImage(r, vp);
    // The text overlay is a normal widget: paint it on top.
    for (QWidget* child : viewport_->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        p.drawPixmap(r.topLeft() + child->pos(), child->grab());
    return img;
}

void MainWindow::newGraph()
{
    auto* g = new GraphPanel(doc_, sim_, this);
    auto* d = new QDockWidget(tr("Graph"), this);
    d->setObjectName(QStringLiteral("graphDock_%1").arg(++graphCount_));
    d->setAttribute(Qt::WA_DeleteOnClose); // graphs are not part of the saved layout
    d->setWidget(g);
    connect(g, &GraphPanel::titleChanged, d, &QDockWidget::setWindowTitle);
    addDockWidget(Qt::BottomDockWidgetArea, d);
    // Bodies graph their speed, other measured items their first channel;
    // with nothing selected the graph shows the scene's total energy.
    int added = 0;
    for (ItemId id : doc_->selection()) {
        const ItemDef* item = doc_->item(id);
        if (!item || added == 6) continue;
        if (item->type->meta().kind == ItemKind::Body) {
            g->addSeries({Quantity::Kind::Body, id, Quantity::Speed, 0});
            ++added;
            continue;
        }
        for (const Quantity& q : Quantity::options(*doc_, id))
            if (q.kind == Quantity::Kind::Channel) {
                g->addSeries(q);
                ++added;
                break;
            }
    }
    if (!added) g->addSeries({Quantity::Kind::Energy, 0, Quantity::Total, 0});

    d->show();
    d->raise();
}

void MainWindow::saveReplay()
{
    if (!sim_->active() || !sim_->recording()) {
        statusBar()->showMessage(tr("Run the simulation with recording on first."), 4000);
        return;
    }
    const ReplayData r = sim_->exportReplay();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Replay"), QString(),
                                                      tr("Kinetic replays (*.kreplay);;All files (*)"));
    if (path.isEmpty()) return;
    ViewState view;
    view.center = viewport_->center();
    view.pixelsPerMetre = viewport_->pixelsPerMetre();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(replayToBytes(r, view)) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Cannot save replay"), tr("%1: %2").arg(path, f.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Saved replay of %1 steps").arg(r.headStep), 4000);
}

void MainWindow::openReplay()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Replay"), QString(),
                                                      tr("Kinetic replays (*.kreplay);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    ReplayFile rf;
    QString err = f.errorString();
    if (!f.open(QIODevice::ReadOnly) || !replayFromBytes(f.readAll(), rf, &err)) {
        QMessageBox::warning(this, tr("Cannot open replay"), tr("%1: %2").arg(QFileInfo(path).fileName(), err));
        return;
    }
    // The document shows every item the replay ever contains (in its latest
    // logged form), so bodies added mid-run have definitions to draw with.
    SceneData scene = rf.scene;
    for (const LoggedCommand& c : rf.replay.log) {
        if (!c.def || c.type == LoggedCommand::Type::SetWorld) continue;
        if (ItemDef* d = scene.find(c.def->id))
            *d = *c.def;
        else
            scene.items.push_back(*c.def);
    }
    for (const ItemDefPtr& d : rf.replay.items)
        if (ItemDef* cur = scene.find(d->id)) *cur = *d; // initial conditions win for items present at the start
    doc_->setFilePath({});
    doc_->reset(std::move(scene));
    showStartPage(false);
    sim_->loadReplay(rf.replay);
    updateTitle();
    statusBar()->showMessage(tr("Replay loaded: %1 steps; drag the timeline or press Play").arg(rf.replay.headStep), 6000);
}

} // namespace kn
