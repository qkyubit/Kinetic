#include "PropertiesPanel.h"

#include "Icons.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace kn {

namespace {

constexpr int kSliderSteps = 1000;

// Slider + spin box for a scalar property. Values are in internal units; the
// spin box shows display units (PropDef::scale) with the unit suffix.
class NumberEditor : public QWidget {
public:
    NumberEditor(const PropDef& pd, bool integer, QWidget* parent) : QWidget(parent), pd_(pd), integer_(integer)
    {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(0, kSliderSteps);
        slider_->setMinimumWidth(40);
        lay->addWidget(slider_, 1);
        const QString suffix = pd.unit.isEmpty() ? QString() : QStringLiteral(" ") + pd.unit;
        if (integer) {
            ispin_ = new QSpinBox(this);
            ispin_->setRange(int(pd.min), int(pd.max));
            ispin_->setSuffix(suffix);
            ispin_->setKeyboardTracking(false);
            ispin_->setMinimumWidth(70);
            lay->addWidget(ispin_);
            QObject::connect(ispin_, &QSpinBox::valueChanged, this, [this](int v) {
                if (updating_) return;
                setSlider(v);
                if (onEdit) onEdit(double(v), false);
            });
        } else {
            spin_ = new QDoubleSpinBox(this);
            spin_->setRange(pd.min * pd.scale, pd.max * pd.scale);
            spin_->setDecimals(pd.decimals);
            spin_->setSuffix(suffix);
            spin_->setKeyboardTracking(false);
            spin_->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
            spin_->setMinimumWidth(80);
            lay->addWidget(spin_);
            QObject::connect(spin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                if (updating_) return;
                setSlider(v / pd_.scale);
                if (onEdit) onEdit(v / pd_.scale, false);
            });
        }
        QObject::connect(slider_, &QSlider::valueChanged, this, [this](int s) {
            if (updating_) return;
            double v = fromSlider(s);
            if (integer_) v = std::round(v);
            setSpin(v);
            if (onEdit) onEdit(v, slider_->isSliderDown());
        });
        QObject::connect(slider_, &QSlider::sliderReleased, this, [this] {
            if (onFinish) onFinish();
        });
        if (!pd.tooltip.isEmpty()) setToolTip(pd.tooltip);
    }

    void setValue(double v)
    {
        updating_ = true;
        setSpin(v);
        setSlider(v);
        updating_ = false;
    }

    std::function<void(double, bool)> onEdit;
    std::function<void()> onFinish;

private:
    bool logScale() const { return pd_.has(PF_Log) && pd_.min > 0.0; }
    int toSlider(double v) const
    {
        double t;
        if (logScale())
            t = std::log(std::max(v, pd_.min) / pd_.min) / std::log(pd_.max / pd_.min);
        else
            t = (v - pd_.min) / std::max(pd_.max - pd_.min, 1e-300);
        return int(std::lround(std::clamp(t, 0.0, 1.0) * kSliderSteps));
    }
    double fromSlider(int s) const
    {
        const double t = double(s) / kSliderSteps;
        if (logScale()) return pd_.min * std::pow(pd_.max / pd_.min, t);
        return pd_.min + t * (pd_.max - pd_.min);
    }
    void setSlider(double v)
    {
        const bool u = updating_;
        updating_ = true;
        slider_->setValue(toSlider(v));
        updating_ = u;
    }
    void setSpin(double v)
    {
        const bool u = updating_;
        updating_ = true;
        if (ispin_) ispin_->setValue(int(std::lround(v)));
        if (spin_) spin_->setValue(v * pd_.scale);
        updating_ = u;
    }

    PropDef pd_;
    bool integer_;
    bool updating_ = false;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* spin_ = nullptr;
    QSpinBox* ispin_ = nullptr;
};

QIcon swatch(Color c)
{
    QPixmap pm(28, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor(128, 128, 128));
    p.setBrush(QColor(c.r, c.g, c.b, c.a));
    p.drawRoundedRect(QRectF(0.5, 0.5, 27, 15), 3, 3);
    return QIcon(pm);
}

// Remembers which sections the user collapsed.
QSet<QString>& collapsedGroups()
{
    static QSet<QString> s;
    return s;
}

} // namespace

PropertiesPanel::PropertiesPanel(Document* doc, SimController* sim, QWidget* parent)
    : QWidget(parent), doc_(doc), sim_(sim)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    lay->addWidget(scroll_);

    connect(doc_, &Document::selectionChanged, this, &PropertiesPanel::rebuild);
    connect(doc_, &Document::sceneReset, this, &PropertiesPanel::rebuild);
    connect(doc_, &Document::itemChanged, this, [this](ItemId id, quint64, bool meta) {
        if (!targets_.contains(id)) return;
        if (meta && nameEdit_ && targets_.size() == 1) nameEdit_->setText(doc_->item(id)->name);
        refresh();
    });
    connect(doc_, &Document::worldChanged, this, [this] {
        if (worldMode_) refresh();
    });
    connect(sim_, &SimController::stateChanged, this, &PropertiesPanel::updateEnabled);
    rebuild();
}

const ItemDef* PropertiesPanel::firstTarget() const
{
    if (worldMode_) return &doc_->world();
    return targets_.isEmpty() ? nullptr : doc_->item(targets_.first());
}

void PropertiesPanel::edit(int index, const PropValue& v, bool continuous)
{
    if (worldMode_)
        doc_->setWorldProperty(index, v, continuous);
    else
        doc_->setProperty(targets_, index, v, continuous);
}

QWidget* PropertiesPanel::makeEditor(const PropDef& pd, int index, Binding& b)
{
    QWidget* w = nullptr;
    switch (pd.type) {
    case PropType::Double:
    case PropType::Int: {
        const bool integer = pd.type == PropType::Int;
        auto* e = new NumberEditor(pd, integer, content_);
        e->onEdit = [this, index, integer](double v, bool cont) {
            if (integer)
                edit(index, int64_t(std::lround(v)), cont);
            else
                edit(index, v, cont);
        };
        e->onFinish = [this] { doc_->endContinuousEdit(); };
        b.show = [e](const PropValue& v) { e->setValue(numericValue(v)); };
        w = e;
        break;
    }
    case PropType::Bool: {
        auto* c = new QCheckBox(content_);
        connect(c, &QCheckBox::toggled, this, [this, index](bool on) { edit(index, on, false); });
        b.show = [c](const PropValue& v) {
            const QSignalBlocker blk(c);
            c->setChecked(std::get<bool>(v));
        };
        w = c;
        break;
    }
    case PropType::Enum: {
        auto* c = new QComboBox(content_);
        c->addItems(pd.options);
        connect(c, &QComboBox::activated, this, [this, index](int i) { edit(index, int64_t(i), false); });
        b.show = [c](const PropValue& v) {
            const QSignalBlocker blk(c);
            c->setCurrentIndex(int(std::get<int64_t>(v)));
        };
        w = c;
        break;
    }
    case PropType::Vector: {
        auto* box = new QWidget(content_);
        auto* lay = new QHBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        QDoubleSpinBox* sp[2];
        for (int k = 0; k < 2; ++k) {
            sp[k] = new QDoubleSpinBox(box);
            sp[k]->setRange(pd.min * pd.scale, pd.max * pd.scale);
            sp[k]->setDecimals(pd.decimals);
            sp[k]->setPrefix(k == 0 ? QStringLiteral("x ") : QStringLiteral("y "));
            if (!pd.unit.isEmpty()) sp[k]->setSuffix(QStringLiteral(" ") + pd.unit);
            sp[k]->setMinimumWidth(60);
            sp[k]->setKeyboardTracking(false);
            sp[k]->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
            lay->addWidget(sp[k], 1);
        }
        auto commit = [this, index, sp, pd] {
            edit(index, Vec2{sp[0]->value() / pd.scale, sp[1]->value() / pd.scale}, false);
        };
        connect(sp[0], &QDoubleSpinBox::valueChanged, this, commit);
        connect(sp[1], &QDoubleSpinBox::valueChanged, this, commit);
        b.show = [sp, pd](const PropValue& v) {
            const Vec2 p = std::get<Vec2>(v);
            for (int k = 0; k < 2; ++k) {
                const QSignalBlocker blk(sp[k]);
                sp[k]->setValue((k == 0 ? p.x : p.y) * pd.scale);
            }
        };
        w = box;
        break;
    }
    case PropType::Colour: {
        auto* btn = new QToolButton(content_);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setIconSize(QSize(28, 16));
        auto current = std::make_shared<Color>();
        connect(btn, &QToolButton::clicked, this, [this, index, current, pd] {
            const QColor init(current->r, current->g, current->b, current->a);
            const QColor c = QColorDialog::getColor(init, this, pd.label, QColorDialog::ShowAlphaChannel);
            if (c.isValid()) edit(index, Color{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(c.alpha())}, false);
        });
        b.show = [btn, current](const PropValue& v) {
            *current = std::get<Color>(v);
            btn->setIcon(swatch(*current));
        };
        w = btn;
        break;
    }
    case PropType::String: {
        auto* le = new QLineEdit(content_);
        connect(le, &QLineEdit::editingFinished, this, [this, index, le] { edit(index, le->text(), false); });
        b.show = [le](const PropValue& v) {
            if (!le->hasFocus()) le->setText(std::get<QString>(v));
        };
        w = le;
        break;
    }
    case PropType::Points: {
        auto* l = new QLabel(content_);
        b.show = [l](const PropValue& v) {
            l->setText(QObject::tr("%1 vertices (edit by drawing)").arg(std::get<PointList>(v).size()));
        };
        w = l;
        break;
    }
    case PropType::ItemRef: {
        auto* c = new QComboBox(content_);
        c->addItem(tr("(world — fixed point)"), QVariant::fromValue<qulonglong>(0));
        for (const ItemDef& d : doc_->scene().items)
            if (pd.refKinds & kindBit(d.type->kind()))
                c->addItem(itemIcon(d.type), d.name, QVariant::fromValue<qulonglong>(d.id));
        connect(c, &QComboBox::activated, this, [this, index, c](int i) {
            edit(index, int64_t(c->itemData(i).toULongLong()), false);
        });
        b.show = [c](const PropValue& v) {
            const QSignalBlocker blk(c);
            const int i = c->findData(QVariant::fromValue<qulonglong>(qulonglong(std::get<int64_t>(v))));
            c->setCurrentIndex(std::max(i, 0));
        };
        w = c;
        break;
    }
    case PropType::Bits: {
        auto* box = new QWidget(content_);
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(1);
        const int n = int(pd.max);
        auto buttons = std::make_shared<std::vector<QToolButton*>>();
        for (int i = 0; i < n; ++i) {
            auto* t = new QToolButton(box);
            t->setText(QString::number(i + 1));
            t->setCheckable(true);
            t->setAutoRaise(false);
            t->setFixedSize(24, 20);
            grid->addWidget(t, i / 8, i % 8);
            buttons->push_back(t);
            connect(t, &QToolButton::toggled, this, [this, index, buttons] {
                int64_t mask = 0;
                for (size_t k = 0; k < buttons->size(); ++k)
                    if ((*buttons)[k]->isChecked()) mask |= int64_t(1) << k;
                edit(index, mask, false);
            });
        }
        b.show = [buttons](const PropValue& v) {
            const int64_t m = std::get<int64_t>(v);
            for (size_t k = 0; k < buttons->size(); ++k) {
                const QSignalBlocker blk((*buttons)[k]);
                (*buttons)[k]->setChecked((m >> k) & 1);
            }
        };
        w = box;
        break;
    }
    }
    if (w && !pd.tooltip.isEmpty()) w->setToolTip(pd.tooltip);
    return w;
}

void PropertiesPanel::rebuild()
{
    bindings_.clear();
    targets_ = doc_->selection();
    worldMode_ = targets_.isEmpty();
    type_ = nullptr;
    nameEdit_ = nullptr;
    typeLabel_ = nullptr;

    content_ = new QWidget;
    auto* lay = new QVBoxLayout(content_);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    const ItemDef* first = firstTarget();
    if (!worldMode_) {
        for (ItemId id : targets_) {
            const ItemDef* d = doc_->item(id);
            if (!type_) type_ = d->type;
            if (d->type != type_) {
                type_ = nullptr;
                break;
            }
        }
    } else {
        type_ = doc_->world().type;
    }

    // Header.
    auto* header = new QWidget(content_);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* icon = new QLabel(header);
    if (type_) icon->setPixmap(itemIcon(type_).pixmap(22, 22));
    hl->addWidget(icon);
    if (worldMode_) {
        auto* l = new QLabel(tr("<b>World settings</b>"), header);
        hl->addWidget(l, 1);
    } else if (targets_.size() == 1) {
        nameEdit_ = new QLineEdit(first->name, header);
        connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
            if (targets_.size() == 1) doc_->rename(targets_.first(), nameEdit_->text());
        });
        hl->addWidget(nameEdit_, 1);
    } else {
        hl->addWidget(new QLabel(tr("<b>%1 items selected</b>").arg(targets_.size()), header), 1);
    }
    lay->addWidget(header);
    if (type_ && !worldMode_) {
        typeLabel_ = new QLabel(type_->meta().description, content_);
        typeLabel_->setWordWrap(true);
        typeLabel_->setForegroundRole(QPalette::PlaceholderText);
        lay->addWidget(typeLabel_);
    }
    if (!type_) {
        lay->addWidget(new QLabel(tr("The selection mixes item types; select items of one type to edit their "
                                     "properties together."),
                                  content_));
        lay->addStretch(1);
        scroll_->setWidget(content_);
        return;
    }

    // Sections.
    const auto& defs = type_->schema().defs();
    QString currentGroup = QStringLiteral("\x01");
    QFormLayout* form = nullptr;
    for (size_t i = 0; i < defs.size(); ++i) {
        const PropDef& pd = defs[i];
        if (pd.has(PF_Hidden)) continue;
        if (pd.group != currentGroup) {
            currentGroup = pd.group;
            const QString key = type_->typeId() + QLatin1Char('/') + pd.group;
            auto* toggle = new QToolButton(content_);
            toggle->setText(pd.group.isEmpty() ? tr("General") : pd.group);
            toggle->setCheckable(true);
            toggle->setChecked(!collapsedGroups().contains(key));
            toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            toggle->setArrowType(toggle->isChecked() ? Qt::DownArrow : Qt::RightArrow);
            toggle->setAutoRaise(true);
            QFont f = toggle->font();
            f.setBold(true);
            toggle->setFont(f);
            lay->addWidget(toggle);
            auto* section = new QWidget(content_);
            form = new QFormLayout(section);
            form->setContentsMargins(12, 0, 0, 4);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            // Narrow docks put the label above its editor instead of clipping.
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            section->setVisible(toggle->isChecked());
            lay->addWidget(section);
            connect(toggle, &QToolButton::toggled, section, [toggle, section, key](bool on) {
                section->setVisible(on);
                toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                if (on)
                    collapsedGroups().remove(key);
                else
                    collapsedGroups().insert(key);
            });
        }
        Binding b{int(i), nullptr, {}};
        QWidget* editor = makeEditor(pd, int(i), b);
        if (!editor) continue;
        b.editor = editor;
        auto* label = new QLabel(pd.label, content_);
        if (!pd.tooltip.isEmpty()) label->setToolTip(pd.tooltip);
        form->addRow(label, editor);
        bindings_.push_back(std::move(b));
    }
    lay->addStretch(1);
    scroll_->setWidget(content_);
    refresh();
    updateEnabled();
}

void PropertiesPanel::refresh()
{
    const ItemDef* d = firstTarget();
    if (!d || !type_ || d->type != type_) return;
    for (Binding& b : bindings_)
        if (b.show) b.show(d->props[size_t(b.index)]);
}

void PropertiesPanel::updateEnabled()
{
    if (!type_) return;
    const bool simActive = sim_->active();
    for (Binding& b : bindings_) {
        const PropDef& pd = type_->schema()[b.index];
        const bool enabled = !pd.has(PF_ReadOnly) && (!simActive || pd.has(PF_Live));
        b.editor->setEnabled(enabled);
        if (simActive && !pd.has(PF_Live)) b.editor->setToolTip(tr("Stop the simulation to edit this property."));
    }
}

} // namespace kn
