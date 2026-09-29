#include "Property.h"

#include <QJsonArray>

namespace kn {

int PropertySchema::indexOf(const QString& key) const
{
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].key == key) return int(i);
    return -1;
}

PropDef& PropertySchema::add(PropDef d)
{
    d.group = group_;
    defs_.push_back(std::move(d));
    return defs_.back();
}

PropDef& PropertySchema::num(const QString& key, const QString& label, const QString& unit, double min, double max,
                             double def, int decimals)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.unit = unit;
    d.type = PropType::Double;
    d.min = min;
    d.max = max;
    d.def = def;
    d.decimals = decimals;
    return add(std::move(d));
}

PropDef& PropertySchema::angle(const QString& key, const QString& label, double minDeg, double maxDeg, double defDeg)
{
    PropDef& d = num(key, label, unit::deg, minDeg / kDegPerRad, maxDeg / kDegPerRad, defDeg / kDegPerRad, 2);
    d.scale = kDegPerRad;
    return d;
}

PropDef& PropertySchema::integer(const QString& key, const QString& label, int64_t min, int64_t max, int64_t def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Int;
    d.min = double(min);
    d.max = double(max);
    d.def = def;
    d.decimals = 0;
    return add(std::move(d));
}

PropDef& PropertySchema::boolean(const QString& key, const QString& label, bool def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Bool;
    d.def = def;
    return add(std::move(d));
}

PropDef& PropertySchema::choice(const QString& key, const QString& label, const QStringList& options, int64_t def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Enum;
    d.options = options;
    d.min = 0;
    d.max = double(options.size() - 1);
    d.def = def;
    return add(std::move(d));
}

PropDef& PropertySchema::vec(const QString& key, const QString& label, const QString& unit, double min, double max,
                             Vec2 def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.unit = unit;
    d.type = PropType::Vector;
    d.min = min;
    d.max = max;
    d.def = def;
    return add(std::move(d));
}

PropDef& PropertySchema::colour(const QString& key, const QString& label, Color def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Colour;
    d.def = def;
    return add(std::move(d));
}

PropDef& PropertySchema::text(const QString& key, const QString& label, const QString& def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::String;
    d.def = def;
    return add(std::move(d));
}

PropDef& PropertySchema::points(const QString& key, const QString& label)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Points;
    d.def = PointList{};
    d.flags = PF_Live | PF_Rebuild;
    return add(std::move(d));
}

PropDef& PropertySchema::ref(const QString& key, const QString& label, uint32_t kinds)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::ItemRef;
    d.def = int64_t(0);
    d.refKinds = kinds;
    d.flags = PF_Live | PF_Rebuild;
    return add(std::move(d));
}

PropDef& PropertySchema::bits(const QString& key, const QString& label, int count, int64_t def)
{
    PropDef d;
    d.key = key;
    d.label = label;
    d.type = PropType::Bits;
    d.def = def;
    d.min = 0;
    d.max = double(count);
    for (int i = 0; i < count; ++i) d.options << QString::number(i + 1);
    return add(std::move(d));
}

double numericValue(const PropValue& v)
{
    if (auto p = std::get_if<double>(&v)) return *p;
    if (auto p = std::get_if<int64_t>(&v)) return double(*p);
    if (auto p = std::get_if<bool>(&v)) return *p ? 1.0 : 0.0;
    if (auto p = std::get_if<Vec2>(&v)) return length(*p);
    return 0.0;
}

PropValue clampValue(const PropDef& d, const PropValue& v)
{
    switch (d.type) {
    case PropType::Double:
        if (auto p = std::get_if<double>(&v)) {
            if (!std::isfinite(*p)) return d.def;
            return std::clamp(*p, d.min, d.max);
        }
        return d.def;
    case PropType::Int:
    case PropType::Enum:
        if (auto p = std::get_if<int64_t>(&v)) return std::clamp<int64_t>(*p, int64_t(d.min), int64_t(d.max));
        return d.def;
    case PropType::Vector:
        if (auto p = std::get_if<Vec2>(&v)) {
            if (!std::isfinite(p->x) || !std::isfinite(p->y)) return d.def;
            return Vec2{std::clamp(p->x, d.min, d.max), std::clamp(p->y, d.min, d.max)};
        }
        return d.def;
    case PropType::Bool:
        return std::holds_alternative<bool>(v) ? v : d.def;
    case PropType::Colour:
        return std::holds_alternative<Color>(v) ? v : d.def;
    case PropType::String:
        return std::holds_alternative<QString>(v) ? v : d.def;
    case PropType::Points:
        return std::holds_alternative<PointList>(v) ? v : d.def;
    case PropType::ItemRef:
    case PropType::Bits:
        return std::holds_alternative<int64_t>(v) ? v : d.def;
    }
    return d.def;
}

static QString colourToString(Color c)
{
    return QStringLiteral("#%1%2%3%4")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'))
        .arg(c.a, 2, 16, QLatin1Char('0'));
}

static bool colourFromString(const QString& s, Color& out)
{
    if (!s.startsWith(QLatin1Char('#')) || (s.size() != 7 && s.size() != 9)) return false;
    bool ok = false;
    const uint v = s.mid(1).toUInt(&ok, 16);
    if (!ok) return false;
    if (s.size() == 7)
        out = Color::fromRgb(v);
    else
        out = Color{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return true;
}

QJsonValue valueToJson(const PropDef& d, const PropValue& v)
{
    switch (d.type) {
    case PropType::Double: return std::get<double>(v);
    case PropType::Int:
    case PropType::ItemRef:
    case PropType::Bits: return double(std::get<int64_t>(v));
    case PropType::Enum: {
        const int64_t i = std::get<int64_t>(v);
        return (i >= 0 && i < d.options.size()) ? QJsonValue(d.options[int(i)]) : QJsonValue(double(i));
    }
    case PropType::Bool: return std::get<bool>(v);
    case PropType::Vector: {
        const Vec2 p = std::get<Vec2>(v);
        return QJsonArray{p.x, p.y};
    }
    case PropType::Colour: return colourToString(std::get<Color>(v));
    case PropType::String: return std::get<QString>(v);
    case PropType::Points: {
        QJsonArray a;
        for (const Vec2& p : std::get<PointList>(v)) a.append(QJsonArray{p.x, p.y});
        return a;
    }
    }
    return {};
}

static bool vecFromJson(const QJsonValue& j, Vec2& out)
{
    if (!j.isArray()) return false;
    const QJsonArray a = j.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) return false;
    out = {a[0].toDouble(), a[1].toDouble()};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

bool valueFromJson(const PropDef& d, const QJsonValue& j, PropValue& out)
{
    switch (d.type) {
    case PropType::Double:
        if (!j.isDouble()) return false;
        out = clampValue(d, j.toDouble());
        return true;
    case PropType::Int:
    case PropType::ItemRef:
    case PropType::Bits:
        if (!j.isDouble()) return false;
        out = clampValue(d, int64_t(j.toDouble()));
        return true;
    case PropType::Enum:
        if (j.isString()) {
            const int i = d.options.indexOf(j.toString());
            if (i < 0) return false;
            out = int64_t(i);
            return true;
        }
        if (!j.isDouble()) return false;
        out = clampValue(d, int64_t(j.toDouble()));
        return true;
    case PropType::Bool:
        if (!j.isBool()) return false;
        out = j.toBool();
        return true;
    case PropType::Vector: {
        Vec2 p;
        if (!vecFromJson(j, p)) return false;
        out = clampValue(d, p);
        return true;
    }
    case PropType::Colour: {
        Color c;
        if (!j.isString() || !colourFromString(j.toString(), c)) return false;
        out = c;
        return true;
    }
    case PropType::String:
        if (!j.isString()) return false;
        out = j.toString();
        return true;
    case PropType::Points: {
        if (!j.isArray()) return false;
        PointList pts;
        for (const QJsonValue& e : j.toArray()) {
            Vec2 p;
            if (!vecFromJson(e, p)) return false;
            pts.push_back(p);
        }
        out = std::move(pts);
        return true;
    }
    }
    return false;
}

QString valueToDisplayString(const PropDef& d, const PropValue& v)
{
    switch (d.type) {
    case PropType::Double: return QString::number(std::get<double>(v) * d.scale, 'f', d.decimals);
    case PropType::Int:
    case PropType::ItemRef:
    case PropType::Bits: return QString::number(std::get<int64_t>(v));
    case PropType::Enum: {
        const int64_t i = std::get<int64_t>(v);
        return (i >= 0 && i < d.options.size()) ? d.options[int(i)] : QString::number(i);
    }
    case PropType::Bool: return std::get<bool>(v) ? QStringLiteral("true") : QStringLiteral("false");
    case PropType::Vector: {
        const Vec2 p = std::get<Vec2>(v);
        return QStringLiteral("(%1, %2)").arg(p.x * d.scale, 0, 'f', d.decimals).arg(p.y * d.scale, 0, 'f', d.decimals);
    }
    case PropType::Colour: return colourToString(std::get<Color>(v));
    case PropType::String: return std::get<QString>(v);
    case PropType::Points: return QStringLiteral("%1 points").arg(std::get<PointList>(v).size());
    }
    return {};
}

} // namespace kn
