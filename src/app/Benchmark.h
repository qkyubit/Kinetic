#pragma once

namespace kn {

// `kinetic --benchmark <scene> --steps N [--warmup W] [--record]`
// Runs the scene headless and prints milliseconds per physics step and heap
// allocation counts (from the malloc interposer).
int runBenchmark(int argc, char** argv);

} // namespace kn
