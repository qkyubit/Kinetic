#include "ConstantsPanel.h"

#include "Document.h"
#include "Icons.h"

#include "core/Runtime.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace kn {

namespace {
constexpr int kSliderSteps = 1000;

int prop(const ItemDef& d, const char* key) { return d.type->schema().indexOf(QLatin1String(key)); }

bool isConstant(const ItemDef& d) { return d.type->kind() == ItemKind::Constant; }
} // namespace

ConstantsPanel::ConstantsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rows_ = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(rows_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(rows_);
    lay->addWidget(scroll, 1);
    empty_ = new QLabel(tr("No constants yet. Constants are named values (such as k or omega) that expressions in "
                           "force laws, paths and ODEs can use."),
                        this);
    empty_->setWordWrap(true);
    lay->addWidget(empty_);
    auto* add = new QPushButton(themedIcon(QStringLiteral("list-add"), QStringLiteral(":/icons/equation.svg")),
                                tr("Add Constant"), this);
    connect(add, &QPushButton::clicked, this, [this] {
        const ItemType* t = ItemRegistry::instance().find(QStringLiteral("equation.constant"));
        ItemDef d = t->makeDefault(doc_->allocateId(), {});
        // A free name: k, k2, k3...
        QStringList used;
        for (const ItemDef& o : doc_->scene().items)
            if (isConstant(o)) used << o.s(prop(o, "symbol"));
        QString name = QStringLiteral("k");
        for (int i = 2; used.contains(name); ++i) name = QStringLiteral("k%1").arg(i);
        d.set(prop(d, "symbol"), name);
        d.name = name;
        doc_->addItems({d}, tr("Add constant"));
    });
    lay->addWidget(add);

    connect(doc_, &Document::sceneReset, this, &ConstantsPanel::rebuild);
    connect(doc_, &Document::itemAdded, this, &ConstantsPanel::rebuild);
    connect(doc_, &Document::itemRemoved, this, &ConstantsPanel::rebuild);
    connect(doc_, &Document::itemChanged, this, [this](ItemId id, quint64 mask, bool meta) {
        const ItemDef* d = doc_->item(id);
        if (!d || !isConstant(*d)) return;
        const quint64 structural = propBit(prop(*d, "symbol")) | propBit(prop(*d, "min")) | propBit(prop(*d, "max")) |
                                   propBit(prop(*d, "unit"));
        if (meta || (mask & structural))
            rebuild();
        else
            refreshValues();
    });
    rebuild();
}

void ConstantsPanel::rebuild()
{
    while (QLayoutItem* it = rowsLayout_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    int n = 0;
    for (const ItemDef& d : doc_->scene().items) {
        if (!isConstant(d)) continue;
        ++n;
        const ItemId id = d.id;
        const int vi = prop(d, "value");
        auto* row = new QWidget(rows_);
        row->setObjectName(QStringLiteral("constant_%1").arg(id));
        auto* g = new QGridLayout(row);
        g->setContentsMargins(0, 4, 0, 4);
        const QString unit = d.s(prop(d, "unit"));
        auto* name = new QLabel(QStringLiteral("<b>%1</b>").arg(d.s(prop(d, "symbol")).toHtmlEscaped()), row);
        name->setToolTip(d.name);
        auto* spin = new QDoubleSpinBox(row);
        spin->setObjectName(QStringLiteral("value"));
        spin->setRange(-1e12, 1e12);
        spin->setDecimals(4);
        spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        if (!unit.isEmpty()) spin->setSuffix(QLatin1Char(' ') + unit);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(QStringLiteral("slider"));
        slider->setRange(0, kSliderSteps);
        const double lo = d.d(prop(d, "min")), hi = d.d(prop(d, "max"));
        slider->setToolTip(tr("%1 to %2").arg(lo).arg(hi));
        g->addWidget(name, 0, 0);
        g->addWidget(spin, 0, 1);
        g->addWidget(slider, 1, 0, 1, 2);
        g->setColumnStretch(1, 1);
        rowsLayout_->addWidget(row);

        connect(slider, &QSlider::valueChanged, this, [this, id, vi, lo, hi](int v) {
            if (updating_) return;
            doc_->setProperty({id}, vi, lo + (hi - lo) * v / kSliderSteps, true);
        });
        connect(slider, &QSlider::sliderReleased, doc_, &Document::endContinuousEdit);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, vi](double v) {
            if (!updating_) doc_->setProperty({id}, vi, v);
        });
    }
    rowsLayout_->addStretch(1);
    empty_->setVisible(n == 0);
    refreshValues();
}

void ConstantsPanel::refreshValues()
{
    updating_ = true;
    for (const ItemDef& d : doc_->scene().items) {
        if (!isConstant(d)) continue;
        auto* row = rows_->findChild<QWidget*>(QStringLiteral("constant_%1").arg(d.id));
        if (!row) continue;
        const double v = d.d(prop(d, "value")), lo = d.d(prop(d, "min")), hi = d.d(prop(d, "max"));
        if (auto* spin = row->findChild<QDoubleSpinBox*>(QStringLiteral("value")); spin && spin->value() != v) spin->setValue(v);
        if (auto* slider = row->findChild<QSlider*>(QStringLiteral("slider")))
            slider->setValue(hi > lo ? int(std::lround(std::clamp((v - lo) / (hi - lo), 0.0, 1.0) * kSliderSteps)) : 0);
    }
    updating_ = false;
}

} // namespace kn
