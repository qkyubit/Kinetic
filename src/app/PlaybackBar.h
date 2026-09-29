// Play/pause, single step, reset, speed (0.1x-10x), recording and the replay timeline.
#pragma once

#include "SimController.h"

#include <QToolBar>

class QAction;
class QComboBox;
class QLabel;
class QSlider;

namespace kn {

class PlaybackBar : public QToolBar {
    Q_OBJECT
public:
    PlaybackBar(SimController* sim, QWidget* parent = nullptr);

    QAction* playAction() const { return play_; }
    QAction* stepAction() const { return step_; }
    QAction* resetAction() const { return reset_; }
    QAction* recordAction() const { return record_; }
    QAction* rewindAction() const { return rewind_; }

private:
    void syncState();
    void syncSnapshot();

    SimController* sim_;
    QAction* play_;
    QAction* step_;
    QAction* reset_;
    QAction* record_;
    QAction* rewind_;
    QComboBox* speed_;
    QSlider* timeline_;
    QLabel* time_;
    QLabel* perf_;
    bool scrubbing_ = false;
};

} // namespace kn
