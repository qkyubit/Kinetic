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
    // Live state of an item in the current snapshot (nullptr when not live or absent).
    const BodySnap* bodySnap(ItemId id) const;
    const ItemSnap* itemSnap(ItemId id) const;
    // True when a simulated item exists in the current simulation frame.
    bool present(ItemId id) const { return bodySnap(id) || itemSnap(id); }

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
    // Moves live bodies (dragging while simulating); the document is untouched.
    void setBodyStates(const std::vector<std::pair<ItemId, Pose>>& poses);
    // Grab tool (the body keeps its physics). `local` is in the body frame.
    void grab(ItemId id, Vec2 local, Vec2 target);
    void moveGrab(Vec2 target);
    void release();

signals:
    void snapshotUpdated();
    void stateChanged();
    void triggerEvents(const std::vector<kn::TriggerEvent>& events);

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

    // Item id -> index into the snapshot arrays (-1: absent). Ids are small and
    // dense, so flat arrays re-index without allocating.
    void ensureIndex() const;
    mutable uint64_t indexedVersion_ = ~0ull;
    mutable uint64_t indexedGeneration_ = ~0ull;
    mutable std::vector<int> bodyIndex_;
    mutable std::vector<int> itemIndex_;
};

} // namespace kn
