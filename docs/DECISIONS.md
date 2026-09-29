# Design decisions

One entry per decision: what was decided, the alternatives, and why.

## Build and dependencies

**Box2D from the Arch repos (3.1.1), with an opt-in pinned fallback.**
Alternatives: always vendor with FetchContent; git submodule.
Arch's `extra/box2d` is 3.1.1, which satisfies "v3.x", so `find_package(box2d 3.1)`
uses it and the PKGBUILD depends on it. `-DKINETIC_FETCH_BOX2D=ON` fetches the same
release pinned by commit (v3.1.1, `8c66146`) for machines without the package; it is
off by default and never used by the PKGBUILD, so `build()` needs no network.

**QRhi through `Qt6::GuiPrivate`.**
Alternatives: QOpenGLWidget, QQuickWidget.
`QRhiWidget` is required. In Qt 6.10+ the `rhi/qrhi.h` headers live in the private
include tree, so the UI library links `Qt6::GuiPrivate`. The QRhi API is
"semi-public" (stable within a minor series); Arch rebuilds dependents when Qt
bumps, which is the normal practice for such packages.

**OpenGL RHI backend (QRhiWidget's Linux default).**
Alternatives: Vulkan. OpenGL on Mesa/Iris is mature on Wayland and X11 and needs no
QVulkanInstance plumbing; nothing here needs Vulkan-only features.

**QtTest instead of GoogleTest/Catch2; Qt JSON instead of nlohmann-json.**
Both already ship in `qt6-base`, keeping the dependency list to Qt + Box2D.

**Allocation counting with a glibc malloc interposer.**
Alternatives: overriding `operator new` only; `b2SetAllocator`; heaptrack.
Defining `malloc`/`calloc`/`realloc`/aligned allocators in the executable (forwarding
to `__libc_*`) counts *every* allocation per thread, including Qt's `QArrayData` and
Box2D's `b2Alloc`, with a thread-local counter. It is compiled in by default
(`KINETIC_ALLOC_COUNTER`) because the overhead is one TLS increment, so the release
benchmark can report allocation counts too.

**Warnings.** `-Wall -Wextra -Wshadow=local`; `-DKINETIC_WERROR=ON` (CI mode) adds `-Werror`.

## Physics

**SI units, 2D densities with an implicit 1 m depth.**
Box2D densities are areal (kg/m²). Material densities are entered in kg/m³ and
used directly as kg/m² for a 1 m thick slab, so a 1 m × 1 m wood box weighs 700 kg
and floating ratios (body density / fluid density) are physical.

**Global gravity via Box2D's world gravity, recorded analytically.**
Alternative: apply `m·g` as an external force. Box2D integrates world gravity inside
every sub-step and uses it for sleeping; the force table records `m·g·gravityScale`
as the tagged "Gravity" component, which is exactly what Box2D applies.

**Contact forces from Box2D impulses divided by the sub-step.**
Box2D v3 stores the *accumulated per-sub-step* impulse in `b2ManifoldPoint::normalImpulse`
/ `tangentImpulse` (it is warm-started and re-applied every sub-step), so the force is
`impulse / (dt / substeps)`. A resting box reports a normal force of exactly `m·g`
(regression-tested). Friction direction is `b2RightPerp(normal)`, as in the solver.

**Static vs kinetic friction by switching each shape's coefficient.**
Box2D has one Coulomb coefficient. After each step every dynamic body is classified as
sticking or sliding (relative tangential speed at any contact > 1 cm/s, measured
relative to conveyor surfaces), and its shapes get μs or μk when the state changes.
Box2D recomputes contact friction every update, so the change takes effect next step.
Mixing between two materials uses Box2D's defaults (geometric mean for friction,
maximum for restitution).

**Single-threaded Box2D.** `workerCount = 1`. Box2D v3 is deterministic with any worker
count, but one thread keeps the physics thread simple; the benchmark shows the 5,000
body stress scene fits the budget (see README).

**The document holds initial conditions; the simulation is a live copy.**
Alternatives: a single scene that *is* the simulation (Algodoo style).
Play builds a world from the document; Reset returns to it. Every edit goes to the
document (undoable) and, while a simulation exists, is also sent to the physics
thread. Editing an initial-state property (pose, velocity) while running re-seats
that body. "Freeze current state into scene" copies the live state into the
document when wanted.

**Force breakdown of sleeping bodies.** Box2D stops reporting contact impulses for
sleeping bodies and creates none for bodies restored asleep, so the last awake
breakdown is kept (and stored in keyframes, display-only) instead of showing a
misleading gravity-only net force.

## Replays and determinism

**Keyframes rebuild the live world too ("re-seating").**
Alternatives: store every frame (hundreds of MB for 10 minutes of 500 bodies);
restore only positions/velocities (replays diverge because Box2D's warm-starting
caches and broad-phase order are not restorable).
While recording, every N steps (default 240) the physics thread captures the full
state (exact float bits of every body, item runtime state) and *rebuilds the world
from it*. Scrubbing rebuilds from the same keyframe and re-simulates with the same
logged edits, so it reproduces the live run bit for bit. The cost is that contact
warm-starting restarts at each keyframe (invisible in practice) and a rebuild
(a few ms for 5,000 bodies) every N steps. Keyframes share immutable item
definitions (`shared_ptr<const ItemDef>`), so they cost ~32 bytes per body.

**Editing while scrubbed back branches the timeline.** The recorded future after the
current step is discarded, like an editor's undo history.

## UI

**Angles shown in degrees, stored in radians.** Angular velocity is shown in rad/s.

**Text via a QPainter overlay widget.** Geometry is drawn with QRhi; labels are values
+ units that the overlay formats, so building a frame does not allocate strings.

**Self-intersecting freehand strokes become their convex hull**, with the outline
updated to match, instead of producing overlapping convex pieces.

**Scene files use the `.kinetic` extension** (indented JSON); `.json` is accepted.
