// Named constant for expressions ("k", "omega"...). Shown as a slider in the
// Constants panel; edits apply live to every expression that uses the name.
#include "EquationItem.h"

namespace kn {

class ConstantItem final : public ItemType {
public:
    ConstantItem()
    {
        meta_ = {QStringLiteral("equation.constant"), QStringLiteral("Constant"), QStringLiteral("Equations"),
                 QStringLiteral("format-number"), QStringLiteral(":/icons/equation.svg"),
                 {QStringLiteral("parameter"), QStringLiteral("slider"), QStringLiteral("variable")},
                 QStringLiteral("A named value that expressions can use, adjustable with a slider."), ItemKind::Constant};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Constant"));
        name_ = s.size();
        s.text(QStringLiteral("symbol"), QStringLiteral("Name in expressions"), QStringLiteral("k"));
        value_ = s.size();
        s.num(QStringLiteral("value"), QStringLiteral("Value"), unit::none, -1e12, 1e12, 1.0, 4);
        min_ = s.size();
        s.num(QStringLiteral("min"), QStringLiteral("Slider minimum"), unit::none, -1e12, 1e12, 0.0, 4);
        max_ = s.size();
        s.num(QStringLiteral("max"), QStringLiteral("Slider maximum"), unit::none, -1e12, 1e12, 10.0, 4);
        unit_ = s.size();
        s.text(QStringLiteral("unit"), QStringLiteral("Unit"), QString());
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }
    bool hasPose() const override { return false; }
    QList<Channel> channels() const override { return {{0, QStringLiteral("Value"), unit::none}}; }

    // Names must be identifiers that are not variables, functions or built-in constants.
    static QString sanitize(QString n)
    {
        for (QChar& c : n)
            if (!c.isLetterOrNumber() && c != QLatin1Char('_')) c = QLatin1Char('_');
        if (n.isEmpty() || n[0].isDigit()) n.prepend(QLatin1Char('_'));
        static const QStringList reserved = {QStringLiteral("t"), QStringLiteral("pi"), QStringLiteral("e")};
        if (reserved.contains(n) || expr::functionNames().contains(n)) n.append(QLatin1Char('_'));
        return n;
    }

    void propertyChanged(ItemDef& d, int index) const override
    {
        if (index == name_ || index < 0) {
            const QString n = sanitize(d.s(name_));
            if (n != d.s(name_)) d.set(name_, n);
        }
        if (d.d(min_) > d.d(max_)) {
            const double lo = d.d(max_), hi = d.d(min_);
            d.set(min_, lo);
            d.set(max_, hi);
        }
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;
    void draw(const ItemDef&, const DrawContext&) const override {}

    int name_, value_, min_, max_, unit_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class ConstantRuntime final : public ItemRuntime {
public:
    ConstantRuntime(const ItemDef& d, const ConstantItem& t, World& w) : ItemRuntime(d.id), t_(t)
    {
        slot_ = w.constantSlot(d.s(t.name_));
        value_ = d.d(t.value_);
        if (slot_ >= 0) w.setConstantValue(slot_, value_);
    }
    bool applyEdit(const ItemDef& d, uint64_t changed, World& w) override
    {
        if (changed & propBit(t_.name_)) return false; // renamed: rebuild (and rebind)
        value_ = d.d(t_.value_);
        if (slot_ >= 0) w.setConstantValue(slot_, value_);
        return true;
    }
    void destroy(World& w) override
    {
        if (slot_ >= 0) w.setConstantValue(slot_, std::nan(""));
    }
    bool snapshot(const World&, ItemSnap& s) const override
    {
        s.v[0] = float(value_);
        return true;
    }

private:
    const ConstantItem& t_;
    int slot_ = -1;
    double value_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> ConstantItem::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<ConstantRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(ConstantItem)

} // namespace kn
