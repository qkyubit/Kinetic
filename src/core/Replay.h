// Replay files: the initial scene plus the logged edit commands, from which
// the simulation is re-run deterministically.
#pragma once

#include "Scene.h"
#include "Sim.h"

namespace kn {

struct ReplayFile {
    SceneData scene; // initial conditions
    ReplayData replay;
};

QByteArray replayToBytes(const ReplayData& r, const ViewState& view);
bool replayFromBytes(const QByteArray& bytes, ReplayFile& out, QString* error);

} // namespace kn
