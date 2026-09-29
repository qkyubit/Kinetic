// Bridges the document (UI thread) and the physics thread: forwards edits as
// commands while a simulation exists and turns published snapshots into signals.
#pragma once

#include "Document.h"

#include "core/Sim.h"

#include <QObject>

namespace kn {

class SimController : public QObject {
    Q_OBJECT
public:
    explicit SimController(Document* doc, QObject* parent = nullptr);
    ~SimController() override;

    // A simulation exists (running or paused). When false the viewport shows the document.
    bool active() const { return active_; }
    bool running() const { return running_; }
    double speed() const { return speed_; }
    bool recording() const { return recording_; }
    // Latest published snapshot. Valid on the UI thread until the next update.
    const Snapshot& snapshot() const { return sim_.snapshot(); }
    // True when the snapshot describes the current simulation (not a stale one).
    bool live() const { return active_ && sim_.snapshot().active && sim_.snapshot().generation == generation_; }

    void play();
    void pause();
    void togglePlay();
    void step(int n = 1);
    void reset();
    void setSpeed(double s);
    void scrubTo(uint64_t step);
    void setRecording(bool on);
    ReplayData exportReplay();
    void loadReplay(const ReplayData& r);

signals:
    void snapshotUpdated();
    void stateChanged();

private:
    void ensureStarted();
    void postEdit(LoggedCommand c);
    void onNotify();

    Document* doc_;
    Simulation sim_;
    bool active_ = false;
    bool running_ = false;
    bool recording_ = true;
    double speed_ = 1.0;
    uint64_t generation_ = 0;
};

} // namespace kn
