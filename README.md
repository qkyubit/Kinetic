# Kinetic

A native 2D physics sandbox for KDE Plasma. Build a scene from rigid bodies,
connectors and mechanisms, fields, liquids, optical elements and equations;
press Play; measure what happens with graphs, force vectors, trails, energy
readouts and trigger timers. Everything is in SI units, and the view scale is
always shown in pixels per metre.

C++20, Qt 6 Widgets (QRhi rendering), Box2D v3.1 for rigid bodies. Fields,
fluids, optics and the expression engine are Kinetic's own.

## Build and run

Dependencies (official Arch repositories only):

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg qt6-shadertools box2d
```

Build, test, run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/kinetic
```

- `kinetic scene.kinetic` opens a scene; `kinetic --example pendulum` opens a
  bundled example by (partial) name.
- `kinetic --benchmark scene.json --steps 2000 [--threads N] [--record]` runs a
  scene headless and prints ms/step and heap allocations per step.
- `kinetic --schema` prints the scene-format reference (all item types and
  properties) as Markdown.
- CI mode: `-DKINETIC_WERROR=ON` adds `-Werror`. Without the `box2d` package,
  `-DKINETIC_FETCH_BOX2D=ON` builds the same Box2D release pinned by commit.

Arch package (builds from this checkout, no network in `build()`):

```bash
cd packaging/arch && makepkg -si
```

The package installs `/usr/bin/kinetic`, `io.github.kinetic.Kinetic.desktop`,
the AppStream metainfo and a scalable hicolor icon.

## Using it

The window has a **Toolbox** (drag or double-click items into the scene;
search by name or tag), the **Explorer** (tree, visibility, locking, groups),
**Properties** (every property editable, most of them live while the
simulation runs), **Analysis** (vectors, trails, energy, field overlay),
**Constants** (sliders for named constants) and a hidden **Trigger Log**
(View menu). **Grab** (H) picks up bodies while simulating: they keep their
physics, swing and spin around the grip, push other bodies, and fly off with
their momentum when you let go. WASD pans the camera (Shift: faster); other
tools: Select Q, Move/Rotate/Scale gizmos T/E/R, Resize box Y (drag a corner or
edge like a window; Shift keeps proportions), Draw B, Ruler M, Protractor O,
Probe P. The ruler and protractor snap to corners, edge midpoints, centres and
edges (vertices win), stay on screen until cleared (Backspace removes the
last, Tools → Clear Measurements all), and points snapped to a body follow it,
so distances and angles update live while simulating. Shift locks them to 15°
steps. The playback bar has Play/Pause (Space), Step, Reset, recording,
speed 0.1×–10× and a timeline you can scrub; editing while scrubbed back
branches the timeline. **File → Save Replay** stores the initial scene and
every edit, and plays back bit-identically. Graphs: select bodies and use
**View → New Graph** (Ctrl+K); any quantity can go on either axis, with CSV
export. Dock layout, theme (Breeze light/dark, live) and settings persist.
**Settings → Configure Physics** sets how long bodies must rest before they
sleep (separately inside wind, vortex and other fields, where a body entering
or leaving wakes the region), the at-rest speed, the world size limit (bodies
beyond it are frozen) and the physics rate.

Examples (File → Open Example, or the start page): pendulum, double pendulum,
projectile with and without drag, Newton's cradle, spring–mass system,
buoyancy comparison, lens and prism optics, two-body orbit, charged particles,
crank–slider, Atwood machine, gear train, conveyors and trampoline, fields/fans/
explosions, Rube Goldberg chain, equations and constants, and a 5,000-body
stress test.

## Architecture

```
            UI thread                                   physics thread
 ┌──────────────────────────────────┐             ┌─────────────────────────────┐
 │ Document (initial conditions,    │             │ SimCore                     │
 │   QUndoStack)                    │  commands   │  World = Box2D world +      │
 │   │ itemAdded/Changed/Removed    │────────────▶│   item runtimes (fields,    │
 │   ▼                              │  SPSC queue │   fluids, connectors, ODEs) │
 │ SimController ── play/pause/     │  (lock-free)│  Recorder: edit log +       │
 │   scrub/edits as LoggedCommands  │             │   keyframes every N steps   │
 │                                  │  snapshots  │  fixed step (120 Hz default)│
 │ Viewport (QRhiWidget, instanced  │◀────────────│  accumulator at 0.1–10×     │
 │   SDF segments/discs, triangles, │ triple buf. │                             │
 │   additive rays; QPainter text)  │  events     │  TaskPool workers: Box2D    │
 │ Graphs, Analysis, Trigger log,   │◀────────────│   solver tasks, contact     │
 │   optics tracer, field overlay   │  SPSC queue │   force accounting          │
 └──────────────────────────────────┘             └─────────────────────────────┘
```

- **The UI never touches live physics state.** Edits become `LoggedCommand`s
  (add, remove, update with a changed-property mask, world settings) posted
  through a lock-free single-producer/single-consumer queue. The physics thread
  publishes complete `Snapshot`s (poses, velocities, accelerations, tagged
  force breakdowns, item channels, energies) into a triple buffer; the UI
  always reads the newest one without blocking or tearing. A generation
  counter discards snapshots from a previous run.
- **Document vs simulation.** The document holds initial conditions and is
  what undo/redo edits. While running, the same edits are forwarded live; the
  core applies them at the current step and logs them.
- **Determinism and scrubbing.** Every `keyframeInterval` steps (default 240)
  the core captures the full state and rebuilds the world from it, even when
  not scrubbing. Replaying from any keyframe therefore takes exactly the same
  path as the live run, so scrubbing, rewinding and replay files reproduce
  frames bit for bit (tested).
- **Force accounting.** Every force carries a source tag (gravity, normal,
  friction, spring, tension, drag, buoyancy, field, electric, magnetic,
  thruster, equation...). Contact forces come from Box2D's normal and
  tangent impulses divided by the sub-step; joint forces from Box2D's
  constraint impulses.
- **No allocation in the hot paths.** Physics steps and frame building reuse
  their buffers. A malloc interposer counts allocations per thread; the
  benchmark and a test check that a physics step makes none (Box2D's own pool
  growth is reported separately).
- **Item system.** Every item kind — bodies, connectors, fields, liquids,
  optical elements, equations, tools — is one `ItemType` subclass in one file
  under `src/items/` plus one line in `src/items/ItemList.h`. A type declares
  metadata, a property schema (which drives the Properties panel, JSON
  serialization, undo and live edits), geometry, drawing and a physics
  runtime.

Source layout: `src/core` (no widgets: math, properties, items, world,
simulation, scene format, expressions, optics tracer), `src/items` (all item
types), `src/app` (Qt Widgets UI), `src/shaders`, `tests`, `tools`
(example generator, screenshots), `packaging`. Design decisions and their
trade-offs are recorded in `docs/DECISIONS.md`.

## Worked example: adding an item type

`src/items/VortexField.cpp` adds a swirling force field. The whole change is
that file plus one line in `src/items/ItemList.h`:

```cpp
KINETIC_ITEM(VortexField)
```

The type: metadata (id, toolbox name, category, icon, search tags,
description) and a schema. `FieldType` contributes position/angle and a
whole-world/rectangle/circle region.

```cpp
class VortexField final : public FieldType {
public:
    VortexField()
        : FieldType({QStringLiteral("field.vortex"), QStringLiteral("Vortex"), QStringLiteral("Fields"),
                     QStringLiteral("view-refresh"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("swirl"), QStringLiteral("whirlpool")},
                     QStringLiteral("Pushes bodies around its centre.")})
    {
        schema_.beginGroup(QStringLiteral("Vortex"));
        strength_ = schema_.size();
        schema_.num(QStringLiteral("strength"), QStringLiteral("Strength"), unit::mps2, -1000, 1000, 15, 2);
        addRegion(Circle, 4, 4, 3);
        addColour(Color::fromRgb(0x00cec9, 60));
    }
    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override
    {
        return std::make_unique<VortexRuntime>(d, *this, w);
    }
    void draw(const ItemDef& d, const DrawContext& ctx) const override { /* region + swirl arrow */ }
    // addField() also feeds the Analysis field overlay.
};
```

The runtime lives on the physics thread. `applyField` runs before every Box2D
step and must not allocate (`FieldRuntime` first works out which bodies are
in the region); forces go through `World` so they are tagged and appear in
force vectors, probes and graphs:

```cpp
class VortexRuntime final : public FieldRuntime {
    void applyField(World& w) override
    {
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i)) continue; // alive, dynamic, awake, centre of mass in the region
            const BodyRec& b = w.body(i);
            const Vec2 a = t_.acceleration(*def_, pose_, {b.com.x, b.com.y});
            w.addFieldForce(i, ForceSource::Field, {float(a.x * b.mass), float(a.y * b.mass)});
        }
    }
};
KINETIC_DEFINE_ITEM(VortexField)
```

That is all: the toolbox entry, Properties panel, undo, copy/paste, scene
files, live edits during simulation, replays and the scene-format reference
come from the schema.

## Scene format

Scenes are JSON (`.kinetic`, `.json` accepted), versioned with
`schemaVersion`; older files are migrated step by step on load (see
`migrateScene` in `src/core/Scene.cpp`), and damaged items are repaired or
skipped with a warning list instead of failing the whole file.

```json
{
  "format": "kinetic-scene",
  "schemaVersion": 2,
  "world": { "gravity": [0, -9.81], "physicsHz": 120, "substeps": 4, "sleep": true },
  "view": { "center": [0, 4], "pixelsPerMetre": 50 },
  "items": [
    { "id": 1, "type": "body.circle", "name": "Bob",
      "props": { "position": [3, 5], "radius": 0.25, "material": 1, "trail": true } },
    { "id": 2, "type": "connector.rod", "name": "String",
      "props": { "bodyA": 1, "anchorA": [0, 0], "bodyB": 0, "anchorB": [0, 5] } }
  ]
}
```

- `id`: positive integer, unique; references (`bodyA`, `bodyB`, `parent`)
  use it, and `0` means "the world".
- `type`: item type id; `props`: property keys from the type's schema. Missing
  properties take their defaults, unknown ones are ignored. Values are SI
  (angles in radians, charges in coulombs); anchors of connectors are in the
  body's local frame (world coordinates when attached to the world).
- Optional per item: `parent` (group id), `visible`, `locked`.
- Derived values (mass from density, lens focal length, expression status) are
  not stored; they are recomputed on load.

Item type ids: `body.box`, `body.circle`, `body.polygon`, `body.capsule`,
`body.freehand`, `body.ground`, `body.wall`, `body.magnet`,
`connector.spring|damper|rope|rod|hinge|slider|pulley|gear|weld`,
`mechanism.motor|conveyor|piston|trampoline|thruster`,
`field.gravity|wind|electric|magnetic|explosion|vortex`, `fluid.region`,
`optics.source|mirror|curvedMirror|thinLens|lens|prism|block|absorber`,
`equation.constant|path|force|ode`, `tool.trigger`, `group`. The complete
reference with every property, type and range is generated by
`kinetic --schema`.

Replay files (`.kreplay`) are `{"format": "kinetic-replay", "version": 1,
"scene": <scene>, "headStep": "N", "commands": [{"step": "N", "type":
"add|remove|update|world", "item": <item>, "id": n, "mask": "hex"}]}`; step
counts and masks are strings because JSON numbers are doubles.

## Expressions

Force laws, parametric paths and ODE systems take expressions compiled by a
hand-written recursive-descent parser to bytecode (no `eval`, no code
execution): `+ - * / % ^` (also `**`), comparisons, `&& || !`, `c ? a : b`,
numbers with exponents, `pi`, `e`, and `sin cos tan asin acos atan atan2 sinh
cosh tanh exp ln log log10 log2 sqrt cbrt abs sign floor ceil round step min
max pow hypot mod clamp lerp`. Variables depend on the item (`t`; `x y vx vy v
m dx dy r` for force laws; the variable names of an ODE). Any other name
refers to a **Constant** item, adjustable with a slider while running. Errors
report the column; sources are limited to 4,096 characters and 85 levels of
nesting; non-finite results are flagged on the item instead of reaching the
physics.

## Performance (Ryzen 5 7600X, Radeon, Plasma 6 Wayland, release build)

| measure | result |
|---|---|
| 5,000-body stress test | 85–87 FPS (vsync-limited), render 0.78 ms/frame, 0 allocations/frame |
| physics step, 5,000 bodies | 1.56 ms mean with 4 workers (3.6 ms single-threaded), 0 allocations/step |
| idle CPU, paused | 0 % (below the 10 ms tick resolution over 10 s) |
| cold start to first frame | 0.64 s first launch, 0.18 s warm |
| release binary | 2.7 MB with Box2D linked statically (excluding Qt) |
| replay memory, 500 bodies × 10 min | 7.6 MB |

## Known limitations

- Liquids are hydrostatic regions (axis-aligned, flat surface, gravity assumed
  downwards): no waves, flow or SPH particles.
- Rigid bodies are transparent to light; optical elements must not overlap,
  and rays start in air. Optics are traced on the UI thread each frame.
- Field forces act on awake bodies only; a body asleep in a newly changed
  field is woken, but bodies can fall asleep inside a field like under
  gravity.
- Mutual gravity is O(n²) over all dynamic bodies.
- Energy drift is only meaningful for conservative scenes; fans, thrusters,
  explosions, drag, motors and region-limited fields are not accounted as
  potentials.
- Magnets are ideal point dipoles at the body's centre; B fields perpendicular
  to the plane exert no torque on them.
- If the UI stalls for more than 8,192 trigger events, the newest ones are
  dropped from the log (the physics thread never blocks on the UI).
- Graph series are part of the window, not the scene file.
- The desktop metadata has no homepage URL yet.
