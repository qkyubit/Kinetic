// Force accounting tags. Every force the simulation applies to a body carries
// one of these so the UI can show components, the net force and energy flows.
#pragma once

#include "Math.h"

#include <cstdint>

namespace kn {

enum class ForceSource : uint8_t {
    Gravity,
    Normal,
    Friction,
    Spring,
    Damper,
    Tension,
    Constraint,
    Motor,
    Drag,
    Buoyancy,
    Wind,
    Field,
    Electric,
    Magnetic,
    Thruster,
    Explosion,
    Equation,
    Damping,
    Hand,
    Count
};

constexpr int kForceSourceCount = int(ForceSource::Count);

inline const char* forceSourceName(ForceSource s)
{
    switch (s) {
    case ForceSource::Gravity: return "Gravity";
    case ForceSource::Normal: return "Normal";
    case ForceSource::Friction: return "Friction";
    case ForceSource::Spring: return "Spring";
    case ForceSource::Damper: return "Damper";
    case ForceSource::Tension: return "Tension";
    case ForceSource::Constraint: return "Constraint";
    case ForceSource::Motor: return "Motor";
    case ForceSource::Drag: return "Drag";
    case ForceSource::Buoyancy: return "Buoyancy";
    case ForceSource::Wind: return "Wind";
    case ForceSource::Field: return "Gravity field";
    case ForceSource::Electric: return "Electric";
    case ForceSource::Magnetic: return "Magnetic";
    case ForceSource::Thruster: return "Thruster";
    case ForceSource::Explosion: return "Explosion";
    case ForceSource::Equation: return "Equation";
    case ForceSource::Damping: return "Damping";
    case ForceSource::Hand: return "Hand";
    case ForceSource::Count: break;
    }
    return "?";
}

// Fixed categorical colours for force arrows (data colours, not UI chrome).
inline Color forceSourceColor(ForceSource s)
{
    static constexpr uint32_t table[kForceSourceCount] = {
        0x8e44ad, // gravity
        0x2e86de, // normal
        0xe67e22, // friction
        0x27ae60, // spring
        0x16a085, // damper
        0xc0392b, // tension
        0x7f8c8d, // constraint
        0xd35400, // motor
        0x95a5a6, // drag
        0x00a8cc, // buoyancy
        0x74b9ff, // wind
        0x6c5ce7, // field
        0xf1c40f, // electric
        0xe84393, // magnetic
        0xff7675, // thruster
        0xd63031, // explosion
        0x00b894, // equation
        0xb2bec3, // damping
        0xfd79a8, // hand (grab tool)
    };
    const int i = int(s);
    return Color::fromRgb(i >= 0 && i < kForceSourceCount ? table[i] : 0x999999);
}

} // namespace kn
