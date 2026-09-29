#pragma once

namespace kn {

// `kinetic --benchmark <scene> --steps N [--warmup W] [--record]`
// Runs the scene headless and prints milliseconds per physics step and heap
// allocation counts (from the malloc interposer).
int runBenchmark(int argc, char** argv);

// `kinetic --schema`: prints the scene-format reference (every item type and
// its properties) as Markdown, generated from the registered schemas.
int printSchemaReference();

} // namespace kn
