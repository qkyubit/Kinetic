// Static/kinetic friction on top of Box2D's single Coulomb coefficient.
//
// Each shape carries its current coefficient in `friction` (the body's static
// or kinetic value, switched by World when the body starts/stops sliding) and a
// `userMaterialId` naming its (static, kinetic) pair in a process-wide table.
// The mixing callback uses kinetic values for *both* surfaces when either one is
// sliding, so a block sliding on static ground gets sqrt(μk·μk') rather than a
// mix of one static and one kinetic coefficient.
#pragma once

namespace kn::friction {

// Registers (or finds) a coefficient pair; returns its material id.
int materialId(float muStatic, float muKinetic);
// b2FrictionCallback: thread-safe, allocation-free.
float mix(float frictionA, int materialA, float frictionB, int materialB);

} // namespace kn::friction
