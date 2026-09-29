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

void SimController::onNotify()
{
    if (sim_.update()) emit snapshotUpdated();
}

} // namespace kn
