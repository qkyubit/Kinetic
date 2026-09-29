// Property schema: every item type declares its editable properties here, and the
// property editor, serializer, undo stack and physics sync are all driven by it.
#pragma once

#include "Math.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <variant>
#include <vector>

namespace kn {

using ItemId = uint32_t;
using PointList = std::vector<Vec2>;

enum class PropType : uint8_t {
    Double,  // double, internal SI units
    Int,     // int64_t
    Bool,    // bool
    Enum,    // int64_t index into PropDef::options
    Vector,  // Vec2
    Colour,  // Color
    String,  // QString
    Points,  // PointList (local-space outline)
    ItemRef, // int64_t item id, 0 = none/world
    Bits,    // int64_t bit mask (collision layers)
};

using PropValue = std::variant<double, int64_t, bool, Vec2, Color, QString, PointList>;

enum PropFlag : uint32_t {
    PF_None = 0,
    PF_Live = 1u << 0,      // editable while the simulation runs
    PF_Hidden = 1u << 1,    // never shown in the property editor
    PF_ReadOnly = 1u << 2,  // shown, not editable (derived display value)
    PF_Log = 1u << 3,       // logarithmic slider
    PF_Transient = 1u << 4, // not serialized; recomputed from other properties
    PF_Rebuild = 1u << 5,   // live change needs the physics object rebuilt
    PF_State = 1u << 6,     // initial condition: live change re-seats pose/velocity
};

struct PropDef {
    QString key;     // stable JSON key
    QString label;   // human readable
    QString unit;    // display unit, e.g. "m", "°"
    QString group;   // editor section
    QString tooltip;
    PropType type = PropType::Double;
    double scale = 1.0; // display value = internal value * scale
    double min = 0.0;   // internal units
    double max = 1.0;
    int decimals = 3;
    uint32_t flags = PF_Live;
    PropValue def = 0.0;
    QStringList options; // enum names, or bit labels
    uint32_t refKinds = 0;

    bool has(uint32_t f) const { return (flags & f) != 0; }
    PropDef& grp(const QString& g) { group = g; return *this; }
    PropDef& tip(const QString& t) { tooltip = t; return *this; }
    PropDef& log() { flags |= PF_Log; return *this; }
    PropDef& rebuild() { flags |= PF_Rebuild; return *this; }
    PropDef& state() { flags |= PF_State; return *this; }
    PropDef& notLive() { flags &= ~PF_Live; return *this; }
    PropDef& readOnly() { flags |= PF_ReadOnly | PF_Transient; return *this; }
    PropDef& hidden() { flags |= PF_Hidden; return *this; }
    PropDef& transient() { flags |= PF_Transient; return *this; }
    PropDef& dec(int d) { decimals = d; return *this; }
};

namespace unit {
inline const QString none;
inline const QString m = QStringLiteral("m");
inline const QString kg = QStringLiteral("kg");
inline const QString s = QStringLiteral("s");
inline const QString N = QStringLiteral("N");
inline const QString deg = QStringLiteral("°");
inline const QString mps = QStringLiteral("m/s");
inline const QString mps2 = QStringLiteral("m/s²");
inline const QString radps = QStringLiteral("rad/s");
inline const QString density = QStringLiteral("kg/m³");
inline const QString stiffness = QStringLiteral("N/m");
inline const QString damping = QStringLiteral("N·s/m");
inline const QString Nm = QStringLiteral("N·m");
inline const QString kgm2 = QStringLiteral("kg·m²");
inline const QString coulomb = QStringLiteral("C");
inline const QString dipole = QStringLiteral("A·m²");
inline const QString hz = QStringLiteral("Hz");
inline const QString nm = QStringLiteral("nm");
inline const QString pas = QStringLiteral("Pa·s");
inline const QString Ns = QStringLiteral("N·s");
inline const QString percent = QStringLiteral("%");
inline const QString persec = QStringLiteral("1/s");
inline const QString m2 = QStringLiteral("m²");
} // namespace unit

class PropertySchema {
public:
    const std::vector<PropDef>& defs() const { return defs_; }
    int size() const { return int(defs_.size()); }
    const PropDef& operator[](int i) const { return defs_[size_t(i)]; }
    // Mutable access for subclasses adjusting inherited defaults.
    PropDef& at(int i) { return defs_[size_t(i)]; }
    int indexOf(const QString& key) const;

    // Builders. Each returns the new definition so modifiers can be chained.
    void beginGroup(const QString& g) { group_ = g; }
    PropDef& num(const QString& key, const QString& label, const QString& unit, double min, double max,
                 double def, int decimals = 3);
    PropDef& angle(const QString& key, const QString& label, double minDeg, double maxDeg, double defDeg);
    PropDef& integer(const QString& key, const QString& label, int64_t min, int64_t max, int64_t def);
    PropDef& boolean(const QString& key, const QString& label, bool def);
    PropDef& choice(const QString& key, const QString& label, const QStringList& options, int64_t def);
    PropDef& vec(const QString& key, const QString& label, const QString& unit, double min, double max, Vec2 def);
    PropDef& colour(const QString& key, const QString& label, Color def);
    PropDef& text(const QString& key, const QString& label, const QString& def);
    PropDef& points(const QString& key, const QString& label);
    PropDef& ref(const QString& key, const QString& label, uint32_t kinds);
    PropDef& bits(const QString& key, const QString& label, int count, int64_t def);

private:
    PropDef& add(PropDef d);
    std::vector<PropDef> defs_;
    QString group_;
};

// Value helpers.
double numericValue(const PropValue& v); // best-effort scalar for graphs/sliders
PropValue clampValue(const PropDef& d, const PropValue& v);
QJsonValue valueToJson(const PropDef& d, const PropValue& v);
// Returns false (and leaves `out` untouched) if the JSON value has the wrong shape.
bool valueFromJson(const PropDef& d, const QJsonValue& j, PropValue& out);
QString valueToDisplayString(const PropDef& d, const PropValue& v);

} // namespace kn
