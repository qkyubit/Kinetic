#include "Sim.h"

#include <chrono>

namespace kn {

// --- Recorder -----------------------------------------------------------------------

void Recorder::reset()
{
    head = 0;
    log.clear();
    keyframes.clear();
    initialItems.clear();
    initialWorld.reset();
}

const Keyframe* Recorder::keyframeAtOrBefore(uint64_t step) const
{
    const auto it = std::upper_bound(keyframes.begin(), keyframes.end(), step,
                                     [](uint64_t s, const Keyframe& k) { return s < k.step; });
    if (it == keyframes.begin()) return nullptr;
    return &*(it - 1);
}

bool Recorder::hasKeyframe(uint64_t step) const
{
    const Keyframe* k = keyframeAtOrBefore(step);
    return k && k->step == step;
}

void Recorder::truncate(uint64_t step)
{
    log.erase(std::remove_if(log.begin(), log.end(), [&](const LoggedCommand& c) { return c.step >= step; }),
              log.end());
    keyframes.erase(std::remove_if(keyframes.begin(), keyframes.end(), [&](const Keyframe& k) { return k.step > step; }),
                    keyframes.end());
    head = step;
}

size_t Recorder::memoryBytes() const
{
    size_t n = sizeof(*this);
    for (const Keyframe& k : keyframes) n += sizeof(k) + k.defs.capacity() * sizeof(ItemDefPtr) + k.state.byteSize();
    n += log.capacity() * sizeof(LoggedCommand);
    return n;
}

// --- SimCore --------------------------------------------------------------------------

void SimCore::start(const SimStart& s)
{
    rec_.reset();
    generation_ = s.generation;
    rec_.enabled = s.recording;
    rec_.initialItems = s.items;
    rec_.initialWorld = s.world;
    rec_.log = s.log;
    rec_.head = s.headStep;
    worldDef_ = s.world;
    world_ = std::make_unique<World>(*worldDef_);
    world_->build(s.items, nullptr);
    seatedAt_ = UINT64_MAX;
    appliedAt_ = UINT64_MAX;
    ensureSeated();
}

void SimCore::stop()
{
    world_.reset();
    rec_.reset();
}

void SimCore::setRecording(bool on)
{
    if (rec_.enabled == on) return;
    rec_.enabled = on;
    if (!on) {
        // Recording off: forget the timeline (it can no longer be reproduced).
        rec_.log.clear();
        rec_.keyframes.clear();
        rec_.head = 0;
    } else if (world_) {
        // Start a fresh timeline from the current state.
        rec_.log.clear();
        rec_.keyframes.clear();
        rec_.head = world_->stepIndex();
        seatedAt_ = UINT64_MAX;
        Keyframe k;
        captureKeyframe(k);
        rebuildFrom(k);
        rec_.keyframes.push_back(std::move(k));
        seatedAt_ = world_->stepIndex();
    }
}

void SimCore::captureKeyframe(Keyframe& k) const
{
    k.step = world_->stepIndex();
    k.time = world_->time();
    k.defs = world_->defs();
    k.world = worldDef_;
    world_->capture(k.state);
    k.energyBaseline = world_->energyBaseline();
    k.baselineValid = world_->energyBaselineValid();
}

void SimCore::rebuildFrom(const Keyframe& k)
{
    worldDef_ = k.world;
    world_ = std::make_unique<World>(*k.world);
    world_->build(k.defs, &k.state);
    world_->setClock(k.step, k.time);
    world_->setEnergyBaseline(k.energyBaseline, k.baselineValid);
}

void SimCore::ensureSeated()
{
    if (!world_ || !rec_.enabled) return;
    const uint64_t s = world_->stepIndex();
    const uint64_t n = uint64_t(world_->settings().keyframeInterval);
    if (s % n != 0 || seatedAt_ == s) return;
    // Re-seat: capture the full state and rebuild the world from it. Replays
    // rebuild from the same data, so they reproduce the live run bit for bit.
    Keyframe k;
    captureKeyframe(k);
    rebuildFrom(k);
    if (!rec_.hasKeyframe(s)) {
        const auto it = std::upper_bound(rec_.keyframes.begin(), rec_.keyframes.end(), s,
                                         [](uint64_t v, const Keyframe& kf) { return v < kf.step; });
        rec_.keyframes.insert(it, std::move(k));
    }
    seatedAt_ = s;
}

void SimCore::apply(const LoggedCommand& c)
{
    switch (c.type) {
    case LoggedCommand::Type::Add: world_->addItem(c.def); break;
    case LoggedCommand::Type::Remove: world_->removeItem(c.id); break;
    case LoggedCommand::Type::Update: world_->updateItem(c.def, c.mask); break;
    case LoggedCommand::Type::SetWorld:
        worldDef_ = c.def;
        world_->setWorldDef(*c.def);
        break;
    case LoggedCommand::Type::SetState: world_->setBodyState(c.id, c.pose, c.v, c.w); break;
    }
}

void SimCore::applyLogged(uint64_t step)
{
    if (appliedAt_ == step) return;
    appliedAt_ = step;
    const auto first = std::lower_bound(rec_.log.begin(), rec_.log.end(), step,
                                        [](const LoggedCommand& c, uint64_t s) { return c.step < s; });
    for (auto it = first; it != rec_.log.end() && it->step == step; ++it) apply(*it);
}

void SimCore::edit(const LoggedCommand& in)
{
    if (!world_) return;
    LoggedCommand c = in;
    c.step = world_->stepIndex();
    if (rec_.enabled) {
        ensureSeated();
        if (c.step < rec_.head) rec_.truncate(c.step); // branch the timeline
        rec_.log.push_back(c);
        rec_.head = std::max(rec_.head, c.step);
        appliedAt_ = c.step; // later logged edits at this step were just made live
    }
    apply(c);
}

void SimCore::advance()
{
    if (!world_) return;
    ensureSeated();
    const uint64_t s = world_->stepIndex();
    if (rec_.enabled && s < rec_.head) applyLogged(s);
    world_->step();
    if (rec_.enabled && world_->stepIndex() > rec_.head) rec_.head = world_->stepIndex();
    // Collect events before a keyframe rebuild replaces the world. Frames that a
    // scrub re-simulates were reported the first time round.
    if (!scrubbing_)
        for (const TriggerEvent& e : world_->events())
            if (events_.size() < events_.capacity()) events_.push_back(e);
    world_->clearEvents();
}

void SimCore::scrubTo(uint64_t target)
{
    if (!world_ || !rec_.enabled) return;
    target = std::min(target, rec_.head);
    const Keyframe* k = rec_.keyframeAtOrBefore(target);
    if (!k) return;
    rebuildFrom(*k);
    seatedAt_ = k->step;
    appliedAt_ = UINT64_MAX;
    scrubbing_ = true;
    while (world_->stepIndex() < target) advance();
    scrubbing_ = false;
}

ReplayData SimCore::exportReplay() const
{
    ReplayData r;
    r.items = rec_.initialItems;
    r.world = rec_.initialWorld;
    r.log = rec_.log;
    r.headStep = rec_.head;
    return r;
}

void SimCore::writeSnapshot(Snapshot& s) const
{
    s.active = world_ != nullptr;
    s.generation = generation_;
    s.recording = rec_.enabled;
    s.headStep = rec_.head;
    if (!world_) {
        s.step = 0;
        s.time = 0;
        s.bodies.clear();
        s.items.clear();
        s.energy = {};
        return;
    }
    world_->writeSnapshot(s);
}

// --- Simulation (physics thread) ----------------------------------------------------------

Simulation::Simulation()
{
    thread_ = std::thread([this] { run(); });
}

Simulation::~Simulation()
{
    SimCommand q;
    q.type = SimCommand::Type::Quit;
    post(std::move(q));
    if (thread_.joinable()) thread_.join();
}

void Simulation::post(SimCommand c)
{
    while (!queue_.push(std::move(c))) std::this_thread::yield();
    wake_.release();
}

bool Simulation::update()
{
    notifyPending_.store(false, std::memory_order_release);
    return snaps_.acquire();
}

ReplayData Simulation::exportReplay()
{
    SimCommand c;
    c.type = SimCommand::Type::ExportReplay;
    c.replay = std::make_shared<std::promise<ReplayData>>();
    auto fut = c.replay->get_future();
    post(std::move(c));
    return fut.get();
}

void Simulation::publish()
{
    for (TriggerEvent e : core_.events()) events_.push(std::move(e)); // drops only if the UI stalls
    core_.clearEvents();
    Snapshot& s = snaps_.back();
    core_.writeSnapshot(s);
    s.running = running_ && core_.active();
    s.speed = speed_;
    s.stepMs = stepMs_;
    snaps_.publish();
    if (!notifyPending_.exchange(true, std::memory_order_acq_rel) && notify_) notify_();
}

void Simulation::handle(SimCommand& c, bool& pub)
{
    switch (c.type) {
    case SimCommand::Type::None: break;
    case SimCommand::Type::Start:
        core_.start(*c.start);
        accumulator_ = 0.0;
        pub = true;
        break;
    case SimCommand::Type::Stop:
        core_.stop();
        running_ = false;
        pub = true;
        break;
    case SimCommand::Type::Play:
        running_ = true;
        accumulator_ = 0.0;
        pub = true;
        break;
    case SimCommand::Type::Pause:
        running_ = false;
        pub = true;
        break;
    case SimCommand::Type::Step:
        running_ = false;
        for (int64_t i = 0; i < std::max<int64_t>(c.arg, 1); ++i) core_.advance();
        pub = true;
        break;
    case SimCommand::Type::SetSpeed:
        speed_ = std::clamp(c.value, 0.01, 100.0);
        pub = true;
        break;
    case SimCommand::Type::Edit:
        core_.edit(c.edit);
        pub = true;
        break;
    case SimCommand::Type::ScrubTo:
        running_ = false;
        core_.scrubTo(uint64_t(std::max<int64_t>(c.arg, 0)));
        pub = true;
        break;
    case SimCommand::Type::SetRecording:
        core_.setRecording(c.arg != 0);
        pub = true;
        break;
    case SimCommand::Type::ExportReplay:
        c.replay->set_value(core_.exportReplay());
        break;
    case SimCommand::Type::Quit: break;
    }
}

void Simulation::run()
{
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    bool quit = false;
    while (!quit) {
        bool pub = false;
        // Drain commands. Consecutive scrubs collapse into the last one.
        SimCommand c;
        SimCommand pendingScrub;
        while (queue_.pop(c)) {
            if (c.type == SimCommand::Type::Quit) {
                quit = true;
                break;
            }
            if (c.type == SimCommand::Type::ScrubTo) {
                pendingScrub = std::move(c);
                continue;
            }
            if (pendingScrub.type == SimCommand::Type::ScrubTo) {
                handle(pendingScrub, pub);
                pendingScrub = {};
            }
            handle(c, pub);
        }
        if (quit) break;
        if (pendingScrub.type == SimCommand::Type::ScrubTo) handle(pendingScrub, pub);

        if (running_ && core_.active()) {
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - last).count();
            last = now;
            const double dt = double(core_.world()->dt());
            accumulator_ += std::min(elapsed, 0.25) * speed_;
            // Budget: never spend more than ~50 ms stepping before publishing,
            // so an overloaded scene runs in slow motion instead of freezing.
            const auto budgetEnd = now + std::chrono::milliseconds(50);
            int steps = 0;
            const auto t0 = Clock::now();
            while (accumulator_ >= dt && Clock::now() < budgetEnd) {
                core_.advance();
                accumulator_ -= dt;
                ++steps;
            }
            if (accumulator_ >= dt) accumulator_ = 0.0; // fell behind: drop the backlog
            if (steps > 0) {
                const float ms = float(std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / steps);
                stepMs_ = stepMs_ == 0.0f ? ms : 0.9f * stepMs_ + 0.1f * ms;
                pub = true;
            }
            if (pub) publish();
            // Sleep until the next step is due (or a command arrives).
            const double wait = std::max(0.0, (dt - accumulator_) / speed_);
            (void)wake_.try_acquire_until(Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                             std::chrono::duration<double>(wait)));
        } else {
            if (pub) publish();
            wake_.acquire();
            last = Clock::now();
        }
    }
}

} // namespace kn
