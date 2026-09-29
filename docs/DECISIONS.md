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

**Box2D runs multithreaded on a small worker pool.**
Alternatives: single-threaded; enkiTS (not in the Arch repos).
Box2D v3 is deterministic for any worker count, so the count is a *machine* setting
(automatic = min(4, logical cores / 2); `KINETIC_THREADS` or `--threads` override),
never stored in scenes. The pool (`TaskPool`) is ~100 lines: workers sleep on a futex
when idle (paused simulation costs no CPU), the physics thread helps with the task
it waits for, which Box2D's lockstep solver tasks require. Claiming work takes a
mutex per block; Box2D issues only a handful of coarse tasks per step, so this is
not measurable and keeps slot reuse race-free. On the development machine the
5,000-body scene went from 3.6 to 1.6 ms per step.

**Contact force accounting runs in parallel too.** Each body's breakdown reads Box2D
and writes only its own record, so a parallel-for over bodies gives identical
results; the Box2D friction switches it may trigger are applied serially afterwards.

**Explicit springs evaluated at a predicted position.**
Alternatives: Box2D distance-joint springs (implicit, but they take a frequency and
use a configuration-dependent effective mass, so a spring constant in N/m cannot be
honoured and the elastic energy is not exactly ½kx²).
External forces are held constant over Box2D's n sub-steps, so a step moves bodies by
(n+1)/(2n)·dt²·F/m while changing their velocity by dt·F/m. That map is not
area-preserving: an explicit spring gains ~0.26 % energy per step at 120 Hz. Evaluating
position-dependent forces at x + α·dt·v with α = (n−1)/(2n) makes the map's
determinant exactly 1 for linear forces (derivation in `World::predictAlpha`), so energy
error stays bounded (±1 % for a 10 rad/s oscillator at 120 Hz) and periods match the
analytic value to 0.03 %. Stiff springs are still limited by explicit integration
(ω·dt < 2); the spring turns red in the viewport when a spring is near that limit.

**Pulleys and gears are force-level constraints.** Box2D v3 removed both joints.
Each step the constraint impulse is solved from the known external forces (gravity,
forces applied earlier in the step, and last step's contact forces) so that the
constraint's velocity error vanishes by the end of the step, plus Baumgarte
correction of the position error. For an Atwood machine this reproduces the analytic
tension exactly (13.0800 N vs 13.0800 N); a block resting on the floor holds the rope
steadily because last step's normal force is part of the estimate.

**Hinges, sliders and motors create the Box2D joint with the bodies swapped**, so the
user-facing convention is "body A moves relative to body B (or the world)": positive
motor speed turns the first-clicked body counter-clockwise, and a slider's
translation is the first body's travel along the axis.

**Pistons are prismatic joints**: the actuator also locks the relative rotation of its
two ends (like a guided linear actuator). Pin the ends with hinges on intermediate
bodies to model a hydraulic ram that pivots.

**Static/kinetic friction mixing.** Box2D's friction callback has no context, and
World switches each dynamic body's shapes between μs and μk. A process-wide,
fixed-capacity table of (μs, μk) pairs is referenced by each shape's
`userMaterialId`; the callback uses the kinetic values of *both* surfaces when either
is sliding (a block sliding on static ground gets √(μk·μk'), not √(μk·μs')). The
table never reallocates, so worker threads read it without locks.

**Contact tuning and restitution threshold are scene settings.** Box2D treats impacts
below 1 m/s as inelastic and its contacts are soft; both are exposed on the World.
The Newton's cradle example uses a 480 Hz physics rate and 5 mm gaps between balls,
which makes the momentum hand-over exact (the last ball leaves at 3.206 m/s for an
analytic 3.21 m/s, the others stay at rest).

**Allocation accounting separates Kinetic and Box2D.** Kinetic's per-step code and the
snapshot publish make zero heap allocations after warm-up (regression-tested). Box2D
allocates when its internal arrays grow past their previous maximum and when islands
fall asleep or wake (each sleeping island gets its own solver set). Those are counted
through `b2SetAllocator` and reported separately; they stop once a scene reaches a
steady state.

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

## Analysis (M3)

**Graphs are UI-side and not persisted.** A graph samples the published
snapshot once per UI frame, so it costs the physics thread nothing. Series live
in the dock, not in the scene file; graph docks are deleted on close and are
left out of the saved window layout. Each graph keeps at most 200,000 rows and
drops the oldest half when full. Scrubbing back truncates the graph at the new
time; the future is re-sampled as it is re-simulated.

**Trails are ring buffers of 2,048 points per body**, sampled from snapshots,
so a trail's resolution depends on the frame rate, not the physics rate.

**Trigger zones are Box2D sensors.** Events are produced on the physics thread
at the end of the step in which the overlap began or ended and are delivered in
order through a lossless SPSC queue (8,192 entries; the physics thread never
blocks, so a UI stalled for more than that many events loses the oldest).
Keyframe rebuilds re-report existing overlaps; the zone keeps its "inside" set
in its saved state and ignores repeats. Events are suppressed while scrubbing
re-simulates from a keyframe. The log replaces its entries from step *s* on
whenever an event for step ≤ the last logged step arrives, so playing on after a
scrub back or an edit does not duplicate rows.

**Replay files (`.kreplay`) store inputs, not states:** the initial scene
(scene schema) plus the edit log (step, add/remove/update/world, item JSON,
changed-property mask as hex). Step counts and masks are strings because JSON
numbers are doubles. Playback is bit-identical because scene doubles round-trip
exactly through Qt's shortest representation and the core is deterministic
(tested). On load, the document shows every item the replay ever contains in
its latest logged form, with initial conditions for items present at step 0.

**Replay memory** is dominated by keyframes (one full world state every
`keyframeInterval` steps, default 240 = 2 s). Measured: 500 awake bodies for
10 minutes at 120 Hz = 300 keyframes, 7.6 MB.

**Event times** are `World::time()`, a sum of single-precision step lengths
(matching Box2D), so they differ from `step × dt` by up to ~1e-7 s per second.

## Fields, fluids and optics (M4)

**Depth convention.** The world is 2D with a nominal depth of 1 m: body mass is
density × area × 1 m (Box2D's convention), so buoyancy (ρ_f A_sub g), drag
(½ ρ Cd W v², W a silhouette width) and charges/moments act per metre of depth
and all balance consistently. Waterlines match Archimedes to 0.12 % (test).

**Field forces act on awake bodies only.** Like gravity, a field does not keep
a resting body awake; field items call `World::wakeAll()` when they are
created or edited so sleeping bodies notice them. Scenes with free charges or
orbits turn sleeping off.

**Pair forces live in World, not in items**: Coulomb forces between charged
bodies, dipole–dipole forces and torques between magnets, and optional mutual
gravity (world setting, gravitational constant adjustable so kilogram-scale
orbits are visible). They are applied equal and opposite, evaluated at the
predicted positions (as springs are), and their potentials are added to the
energy readout. All three use the world's softening length ε (r² → r² + ε²).
Mutual gravity is O(n²) over all dynamic bodies and is off by default.

**Region fields** (gravity zones, wind/fans, uniform E and B) test a body's
centre of mass against the region. Their potential energy is only counted for
whole-world regions; a region boundary would make it discontinuous, and fans
and thrusters are not conservative, so the drift readout is meaningless in such
scenes.

**Uniform magnetic fields rotate velocities exactly.** An explicit q v × B force
with sub-stepping spirals outwards; applying the force that rotates the
velocity by −qBΔt/m over the step keeps |v| constant to 1e-6 (test: cyclotron
radius within 0.01 %). B is perpendicular to the plane, so it exerts no torque
on in-plane magnets.

**Explicit drag is clamped** so that it can at most stop a body within one step
(light bodies in thick liquids or strong wind would otherwise reverse).

**Fluids are hydrostatic regions, not SPH.** A liquid is an axis-aligned
rectangle whose top edge is the free surface (gravity assumed to point down).
Buoyancy uses the exact submerged area and its centroid (polygons exactly;
circles exactly while between the side walls, 64-gons otherwise); drag is
k μ W v + ½ ρ Cd W |v| v with k = 6 and W scaled by the submerged fraction,
plus a matching rotational term. There are no waves, no displacement of the
surface and no flow. A particle (SPH) liquid was left out: it would cost the
5,000-body frame budget and cannot give the exact waterlines the acceptance
tests require.

**Optics are traced on the UI thread from the document** every frame (they do
not interact with rigid bodies, which are transparent to light). The tracer
handles segments and exact circular arcs, so spherical lenses have no
faceting error (paraxial back focal distance within 0.01 % of the thick-lens
lensmaker equation). Refracting elements must not overlap: the tracer tracks
which element a ray is inside by the boundary it crossed. Rays start in air.
Fresnel reflection (unpolarised average of s and p) splits each refraction;
branches below 1 % intensity, deeper than 64 interactions or beyond 60,000
segments in total are dropped. Glasses use Schott/Malitson Sellmeier
coefficients (N-BK7, F2, SF11, fused silica, diamond) or Cauchy fits.

**White light** is sampled at N evenly spaced wavelengths in 400–700 nm (default
12). Each sample's colour is the CIE 1931 (multi-lobe fit) hue in sRGB,
weighted per channel so the samples add back to white. On dark themes rays use
the additive pipeline, so undispersed white light looks white and a spectrum
fans out; additive light is invisible on a light canvas, so light themes draw
each ray in its own hue with normal blending.
