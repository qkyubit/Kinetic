// Measurable quantities for graphs and probes: body kinematics and dynamics,
// per-source forces, item channels (tension, angle...), any numeric property,
// and the scene's energies.
#pragma once

#include "Document.h"
#include "SimController.h"

namespace kn {

struct Quantity {
    enum class Kind : uint8_t { Time, Body, Channel, Property, Energy };
    enum Body : int {
        X, Y, Angle, Vx, Vy, Speed, Omega, Ax, Ay, Accel, Alpha, KE, PEg, Px, Py, P, L, Fx, Fy, F, SourceForce
    };
    enum Energy : int { Kinetic, Gravity, Spring, Field, Total, Drift };

    Kind kind = Kind::Time;
    ItemId item = 0;
    int code = 0; // Body / Energy enum, channel index, property index
    int arg = 0;  // force source for SourceForce

    bool operator==(const Quantity&) const = default;

    QString name(const Document& doc) const; // "speed"
    QString label(const Document& doc) const; // "Box 3 · speed"
    QString unit(const Document& doc) const;
    // Current value from the live snapshot (and the document for properties).
    bool eval(const SimController& sim, const Document& doc, double& out) const;

    // Everything that can be measured on an item (item 0: time and energies).
    static QList<Quantity> options(const Document& doc, ItemId item);
};

} // namespace kn
