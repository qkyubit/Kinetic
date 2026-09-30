#include "SimController.h"

#include <QMetaObject>

namespace kn {

SimController::SimController(Document* doc, QObject* parent) : QObject(parent), doc_(doc)
{
    // Called on the physics thread; hop to the UI thread (coalesced by Simulation).
    sim_.setNotify([this] { QMetaObject::invokeMethod(this, &SimController::onNotify, Qt::QueuedConnection); });

    connect(doc_, &Document::itemAdded, this, [this](ItemId id) {
        if (!active_) return;
        LoggedCommand c;
        c.type = LoggedCommand::Type::Add;
        c.def = std::make_shared<ItemDef>(*doc_->item(id));
        postEdit(std::move(c));
    });
    connect(doc_, &Document::itemRemoved, this, [this](ItemId id) {
        if (!active_) return;
        LoggedCommand c;
        c.type = LoggedCommand::Type::Remove;
        c.id = id;
        postEdit(std::move(c));
    });
    connect(doc_, &Document::itemChanged, this, [this](ItemId id, quint64 mask, bool) {
        if (!active_ || mask == 0) return;
        LoggedCommand c;
        c.type = LoggedCommand::Type::Update;
        c.def = std::make_shared<ItemDef>(*doc_->item(id));
        c.mask = mask;
        postEdit(std::move(c));
    });
    connect(doc_, &Document::worldChanged, this, [this](quint64 mask) {
        if (!active_ || mask == 0) return;
        LoggedCommand c;
        c.type = LoggedCommand::Type::SetWorld;
        c.def = std::make_shared<ItemDef>(doc_->world());
        postEdit(std::move(c));
    });
    connect(doc_, &Document::sceneReset, this, &SimController::reset);
}

SimController::~SimController() = default;

void SimController::postEdit(LoggedCommand c)
{
    SimCommand cmd;
    cmd.type = SimCommand::Type::Edit;
    cmd.edit = std::move(c);
    sim_.post(std::move(cmd));
}

void SimController::ensureStarted()
{
    if (active_) return;
    auto start = std::make_shared<SimStart>();
    for (const ItemDef& d : doc_->scene().items) start->items.push_back(std::make_shared<ItemDef>(d));
    start->world = std::make_shared<ItemDef>(doc_->world());
    start->recording = recording_;
    start->generation = ++generation_;
    SimCommand c;
    c.type = SimCommand::Type::Start;
    c.start = std::move(start);
    sim_.post(std::move(c));
    active_ = true;
}

void SimController::play()
{
    ensureStarted();
    SimCommand c;
    c.type = SimCommand::Type::Play;
    sim_.post(std::move(c));
    running_ = true;
    emit stateChanged();
}

void SimController::pause()
{
    if (!active_) return;
    SimCommand c;
    c.type = SimCommand::Type::Pause;
    sim_.post(std::move(c));
    running_ = false;
    emit stateChanged();
}

void SimController::togglePlay()
{
    if (running_)
        pause();
    else
        play();
}

void SimController::step(int n)
{
    ensureStarted();
    SimCommand c;
    c.type = SimCommand::Type::Step;
    c.arg = n;
    sim_.post(std::move(c));
    running_ = false;
    emit stateChanged();
}

void SimController::reset()
{
    if (!active_) return;
    SimCommand c;
    c.type = SimCommand::Type::Stop;
    sim_.post(std::move(c));
    active_ = false;
    running_ = false;
    emit stateChanged();
    emit snapshotUpdated();
}

void SimController::setSpeed(double s)
{
    speed_ = std::clamp(s, 0.1, 10.0);
    SimCommand c;
    c.type = SimCommand::Type::SetSpeed;
    c.value = speed_;
    sim_.post(std::move(c));
    emit stateChanged();
}

void SimController::scrubTo(uint64_t step)
{
    if (!active_) return;
    SimCommand c;
    c.type = SimCommand::Type::ScrubTo;
    c.arg = int64_t(step);
    sim_.post(std::move(c));
    if (running_) {
        running_ = false;
        emit stateChanged();
    }
}

void SimController::setRecording(bool on)
{
    recording_ = on;
    if (active_) {
        SimCommand c;
        c.type = SimCommand::Type::SetRecording;
        c.arg = on ? 1 : 0;
        sim_.post(std::move(c));
    }
    emit stateChanged();
}

ReplayData SimController::exportReplay()
{
    if (!active_) return {};
    return sim_.exportReplay();
}

void SimController::loadReplay(const ReplayData& r)
{
    reset();
    auto start = std::make_shared<SimStart>();
    start->items = r.items;
    start->world = r.world;
    start->log = r.log;
    start->headStep = r.headStep;
    start->recording = true;
    start->generation = ++generation_;
    recording_ = true;
    SimCommand c;
    c.type = SimCommand::Type::Start;
    c.start = std::move(start);
    sim_.post(std::move(c));
    active_ = true;
    running_ = false;
    emit stateChanged();
}

void SimController::setBodyStates(const std::vector<std::pair<ItemId, Pose>>& poses)
{
    if (!active_) return;
    for (const auto& [id, pose] : poses) {
        LoggedCommand c;
        c.type = LoggedCommand::Type::SetState;
        c.id = id;
        c.pose = pose;
        postEdit(std::move(c));
    }
}

void SimController::grab(ItemId id, Vec2 local, Vec2 target)
{
    ensureStarted();
    LoggedCommand c;
    c.type = LoggedCommand::Type::Grab;
    c.id = id;
    c.v = local;
    c.pose.p = target;
    postEdit(std::move(c));
}

void SimController::moveGrab(Vec2 target)
{
    if (!active_) return;
    LoggedCommand c;
    c.type = LoggedCommand::Type::GrabMove;
    c.pose.p = target;
    postEdit(std::move(c));
}

void SimController::release()
{
    if (!active_) return;
    LoggedCommand c;
    c.type = LoggedCommand::Type::Release;
    postEdit(std::move(c));
}

void SimController::onNotify()
{
    const bool fresh = sim_.update();
    std::vector<TriggerEvent> events;
    TriggerEvent e;
    while (sim_.popEvent(e)) events.push_back(e);
    if (fresh) emit snapshotUpdated();
    if (!events.empty()) emit triggerEvents(events);
}

void SimController::ensureIndex() const
{
    const Snapshot& s = sim_.snapshot();
    // Buffers rotate, but with an unchanged structure the ordering is the same.
    if (s.structureVersion == indexedVersion_ && s.generation == indexedGeneration_) return;
    indexedVersion_ = s.structureVersion;
    indexedGeneration_ = s.generation;
    auto rebuild = [](std::vector<int>& index, const auto& vec) {
        std::fill(index.begin(), index.end(), -1);
        for (size_t i = 0; i < vec.size(); ++i) {
            const ItemId id = vec[i].id;
            if (id >= index.size()) index.resize(size_t(id) * 2 + 64, -1); // grows only with the scene
            index[id] = int(i);
        }
    };
    rebuild(bodyIndex_, s.bodies);
    rebuild(itemIndex_, s.items);
}

const BodySnap* SimController::bodySnap(ItemId id) const
{
    if (!live()) return nullptr;
    ensureIndex();
    if (id >= bodyIndex_.size() || bodyIndex_[id] < 0) return nullptr;
    const auto& b = sim_.snapshot().bodies;
    const size_t i = size_t(bodyIndex_[id]);
    return i < b.size() && b[i].id == id ? &b[i] : nullptr;
}

const ItemSnap* SimController::itemSnap(ItemId id) const
{
    if (!live()) return nullptr;
    ensureIndex();
    if (id >= itemIndex_.size() || itemIndex_[id] < 0) return nullptr;
    const auto& v = sim_.snapshot().items;
    const size_t i = size_t(itemIndex_[id]);
    return i < v.size() && v[i].id == id ? &v[i] : nullptr;
}

} // namespace kn
