// Simulation core (deterministic stepping, command log, keyframes) and the
// physics thread that drives it.
//
//   UI thread --SimCommand--> [SPSC queue] --> physics thread (SimCore, World)
//   UI thread <--Snapshot---- [triple buffer] <-- physics thread
#pragma once

#include "Concurrency.h"
#include "Snapshot.h"
#include "World.h"

#include <QJsonObject>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <semaphore>
#include <thread>

namespace kn {

// An edit that changes simulation state; these are logged for replays.
struct LoggedCommand {
    enum class Type : uint8_t { Add, Remove, Update, SetWorld };
    uint64_t step = 0;
    Type type = Type::Update;
    ItemDefPtr def;   // Add, Update, SetWorld
    ItemId id = 0;    // Remove
    uint64_t mask = 0; // Update: changed property bits
};

struct SimStart {
    std::vector<ItemDefPtr> items;
    ItemDefPtr world;
    bool recording = true;
    std::vector<LoggedCommand> log; // non-empty when loading a replay
    uint64_t headStep = 0;
    uint64_t generation = 0; // echoed in snapshots so the UI can ignore stale ones
};

struct ReplayData {
    std::vector<ItemDefPtr> items; // initial scene
    ItemDefPtr world;
    std::vector<LoggedCommand> log;
    uint64_t headStep = 0;
};

struct SimCommand {
    enum class Type : uint8_t {
        None,
        Start,
        Stop,
        Play,
        Pause,
        Step,
        SetSpeed,
        Edit,
        ScrubTo,
        SetRecording,
        ExportReplay,
        Quit,
    };
    Type type = Type::None;
    LoggedCommand edit;                      // Edit
    int64_t arg = 0;                         // Step count, ScrubTo step
    double value = 1.0;                      // speed
    std::shared_ptr<const SimStart> start;   // Start
    std::shared_ptr<std::promise<ReplayData>> replay; // ExportReplay
};

struct Keyframe {
    uint64_t step = 0;
    double time = 0.0;
    std::vector<ItemDefPtr> defs;
    ItemDefPtr world;
    RestoreState state;
    double energyBaseline = 0.0;
    bool baselineValid = false;
};

class Recorder {
public:
    bool enabled = true;
    uint64_t head = 0;
    std::vector<LoggedCommand> log; // ordered by step
    std::vector<Keyframe> keyframes; // ordered by step
    std::vector<ItemDefPtr> initialItems;
    ItemDefPtr initialWorld;

    void reset();
    const Keyframe* keyframeAtOrBefore(uint64_t step) const;
    bool hasKeyframe(uint64_t step) const;
    // Drops the recorded future after `step` (branching on an edit).
    void truncate(uint64_t step);
    size_t memoryBytes() const;
};

// Deterministic simulation driver. Used directly by headless tests and the
// benchmark, and by Simulation on the physics thread.
class SimCore {
public:
    void start(const SimStart& s);
    void stop();
    bool active() const { return world_ != nullptr; }
    // An edit coming from the UI at the current step.
    void edit(const LoggedCommand& c);
    // Advances one physics step (replaying logged edits when behind the head).
    void advance();
    // Reconstructs the state after `step` steps from the nearest keyframe.
    void scrubTo(uint64_t step);
    void setRecording(bool on);
    uint64_t step() const { return world_ ? world_->stepIndex() : 0; }
    World* world() { return world_.get(); }
    const World* world() const { return world_.get(); }
    const Recorder& recorder() const { return rec_; }
    ReplayData exportReplay() const;
    void writeSnapshot(Snapshot& s) const;
    // Trigger events produced by advance() since the last clearEvents().
    const std::vector<TriggerEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }

private:
    void ensureSeated();
    void applyLogged(uint64_t step);
    void apply(const LoggedCommand& c);
    void rebuildFrom(const Keyframe& k);
    void captureKeyframe(Keyframe& k) const;

    std::unique_ptr<World> world_;
    ItemDefPtr worldDef_;
    Recorder rec_;
    uint64_t generation_ = 0;
    uint64_t seatedAt_ = UINT64_MAX;
    uint64_t appliedAt_ = UINT64_MAX;
    bool scrubbing_ = false;
    std::vector<TriggerEvent> events_ = [] {
        std::vector<TriggerEvent> v;
        v.reserve(8192);
        return v;
    }();
};

class Simulation {
public:
    Simulation();
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // UI thread.
    void post(SimCommand c);
    // Makes the newest published snapshot current. Returns true if it changed.
    bool update();
    const Snapshot& snapshot() const { return snaps_.front(); }
    // Called on the physics thread after a publish; at most one call is
    // outstanding until the UI calls update().
    void setNotify(std::function<void()> fn) { notify_ = std::move(fn); }
    ReplayData exportReplay();
    // Trigger events, delivered in order (UI thread).
    bool popEvent(TriggerEvent& e) { return events_.pop(e); }

private:
    void run();
    void handle(SimCommand& c, bool& publish);
    void publish();

    SpscQueue<SimCommand, 1024> queue_;
    SpscQueue<TriggerEvent, 8192> events_;
    TripleBuffer<Snapshot> snaps_;
    std::counting_semaphore<1 << 20> wake_{0};
    std::atomic<bool> notifyPending_{false};
    std::function<void()> notify_;
    SimCore core_;
    bool running_ = false;
    double speed_ = 1.0;
    double accumulator_ = 0.0;
    float stepMs_ = 0.0f;
    std::thread thread_;
};

} // namespace kn
