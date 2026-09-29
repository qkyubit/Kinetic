#include "PlaybackBar.h"

#include "Icons.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

namespace kn {

PlaybackBar::PlaybackBar(SimController* sim, QWidget* parent) : QToolBar(tr("Playback"), parent), sim_(sim)
{
    setObjectName(QStringLiteral("playbackBar"));
    rewind_ = addAction(themedIcon(QStringLiteral("media-seek-backward"), QStringLiteral(":/icons/rewind.svg")),
                        tr("Rewind to Start"));
    rewind_->setToolTip(tr("Rewind the recording to the first frame"));
    play_ = addAction(themedIcon(QStringLiteral("media-playback-start"), QStringLiteral(":/icons/play.svg")), tr("Play"));
    play_->setShortcut(Qt::Key_Space);
    play_->setShortcutContext(Qt::WindowShortcut);
    step_ = addAction(themedIcon(QStringLiteral("media-skip-forward"), QStringLiteral(":/icons/step.svg")),
                      tr("Single Step"));
    step_->setShortcut(QKeySequence(Qt::Key_Period));
    reset_ = addAction(themedIcon(QStringLiteral("media-playback-stop"), QStringLiteral(":/icons/stop.svg")),
                       tr("Reset Simulation"));
    reset_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    reset_->setToolTip(tr("Stop and return to the scene's initial state (Ctrl+R)"));
    record_ = addAction(themedIcon(QStringLiteral("media-record"), QStringLiteral(":/icons/record.svg")), tr("Record"));
    record_->setCheckable(true);
    record_->setChecked(sim_->recording());
    record_->setToolTip(tr("Record keyframes and edits so the run can be scrubbed and replayed exactly"));
    addSeparator();

    addWidget(new QLabel(tr(" Speed "), this));
    speed_ = new QComboBox(this);
    for (double s : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0}) speed_->addItem(QStringLiteral("%1×").arg(s), s);
    speed_->setCurrentIndex(3);
    speed_->setToolTip(tr("Simulation speed relative to real time"));
    addWidget(speed_);
    addSeparator();

    timeline_ = new QSlider(Qt::Horizontal, this);
    timeline_->setMinimumWidth(160);
    timeline_->setRange(0, 0);
    timeline_->setToolTip(tr("Recorded timeline: drag to scrub to any frame"));
    addWidget(timeline_);
    time_ = new QLabel(this);
    time_->setMinimumWidth(180);
    time_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addWidget(time_);
    perf_ = new QLabel(this);
    perf_->setForegroundRole(QPalette::PlaceholderText);
    addWidget(perf_);

    connect(play_, &QAction::triggered, sim_, &SimController::togglePlay);
    connect(step_, &QAction::triggered, sim_, [this] { sim_->step(1); });
    connect(reset_, &QAction::triggered, sim_, &SimController::reset);
    connect(record_, &QAction::toggled, sim_, &SimController::setRecording);
    connect(rewind_, &QAction::triggered, sim_, [this] { sim_->scrubTo(0); });
    connect(speed_, &QComboBox::activated, this, [this](int i) { sim_->setSpeed(speed_->itemData(i).toDouble()); });
    connect(timeline_, &QSlider::sliderPressed, this, [this] { scrubbing_ = true; });
    connect(timeline_, &QSlider::sliderReleased, this, [this] { scrubbing_ = false; });
    connect(timeline_, &QSlider::valueChanged, this, [this](int v) {
        if (scrubbing_ || timeline_->isSliderDown()) sim_->scrubTo(uint64_t(v));
    });
    connect(sim_, &SimController::stateChanged, this, &PlaybackBar::syncState);
    connect(sim_, &SimController::snapshotUpdated, this, &PlaybackBar::syncSnapshot);
    syncState();
    syncSnapshot();
}

void PlaybackBar::syncState()
{
    const bool running = sim_->running();
    play_->setText(running ? tr("Pause") : tr("Play"));
    play_->setIcon(running ? themedIcon(QStringLiteral("media-playback-pause"), QStringLiteral(":/icons/pause.svg"))
                           : themedIcon(QStringLiteral("media-playback-start"), QStringLiteral(":/icons/play.svg")));
    play_->setToolTip(running ? tr("Pause (Space)") : tr("Play (Space)"));
    reset_->setEnabled(sim_->active());
    rewind_->setEnabled(sim_->active() && sim_->recording());
    timeline_->setEnabled(sim_->active() && sim_->recording());
    {
        const QSignalBlocker b(record_);
        record_->setChecked(sim_->recording());
    }
    syncSnapshot();
}

void PlaybackBar::syncSnapshot()
{
    if (!sim_->live()) {
        time_->setText(sim_->active() ? tr("starting…") : tr("t = 0 s  (editing)"));
        perf_->clear();
        const QSignalBlocker b(timeline_);
        timeline_->setRange(0, 0);
        return;
    }
    const Snapshot& s = sim_->snapshot();
    QString t = tr("t = %1 s   step %2").arg(s.time, 0, 'f', 3).arg(s.step);
    if (s.recording && s.step < s.headStep) t += tr("   (replay: %1 / %2)").arg(s.step).arg(s.headStep);
    time_->setText(t);
    perf_->setText(s.stepMs > 0 ? tr("  %1 bodies · %2 ms/step").arg(s.bodies.size()).arg(s.stepMs, 0, 'f', 2)
                                : tr("  %1 bodies").arg(s.bodies.size()));
    if (!timeline_->isSliderDown()) {
        const QSignalBlocker b(timeline_);
        timeline_->setRange(0, int(std::min<uint64_t>(s.headStep, INT32_MAX)));
        timeline_->setValue(int(std::min<uint64_t>(s.step, INT32_MAX)));
    }
}

} // namespace kn
