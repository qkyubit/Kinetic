#include "AnalysisPanel.h"

#include "Analysis.h"
#include "Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace kn {

AnalysisPanel::AnalysisPanel(Analysis* analysis, Document* doc, QWidget* parent)
    : QWidget(parent), analysis_(analysis), doc_(doc)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    const AnalysisOptions& o = analysis_->options();
    auto update = [this](auto fn) {
        return [this, fn](auto value) {
            AnalysisOptions opts = analysis_->options();
            fn(opts, value);
            analysis_->setOptions(opts);
        };
    };

    // Vectors.
    auto* vectors = new QGroupBox(tr("Vectors"), this);
    auto* vf = new QFormLayout(vectors);
    vf->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* forces = new QComboBox(vectors);
    forces->addItems({tr("Off"), tr("Flagged and selected bodies"), tr("All dynamic bodies")});
    forces->setCurrentIndex(int(o.forces));
    forces->setToolTip(tr("Bodies are flagged with their \"Show force vectors\" property."));
    vf->addRow(tr("Forces"), forces);
    connect(forces, &QComboBox::currentIndexChanged, this,
            update([](AnalysisOptions& op, int i) { op.forces = AnalysisOptions::ForceMode(i); }));
    auto check = [&](const QString& text, bool value, auto setter) {
        auto* c = new QCheckBox(text, vectors);
        c->setChecked(value);
        connect(c, &QCheckBox::toggled, this, update(setter));
        vf->addRow(c);
        return c;
    };
    check(tr("Force components (colour = source)"), o.components, [](AnalysisOptions& op, bool v) { op.components = v; });
    check(tr("Net force"), o.net, [](AnalysisOptions& op, bool v) { op.net = v; });
    check(tr("Velocity"), o.velocity, [](AnalysisOptions& op, bool v) { op.velocity = v; });
    check(tr("Acceleration"), o.acceleration, [](AnalysisOptions& op, bool v) { op.acceleration = v; });
    check(tr("Magnitude labels"), o.labels, [](AnalysisOptions& op, bool v) { op.labels = v; });

    auto* scaleMode = new QComboBox(vectors);
    scaleMode->addItems({tr("Relative to the body's weight"), tr("Absolute")});
    scaleMode->setCurrentIndex(int(o.scaleMode));
    vf->addRow(tr("Force scale"), scaleMode);
    auto spin = [&](double value, double lo, double hi, int decimals, const QString& suffix, auto setter) {
        auto* s = new QDoubleSpinBox(vectors);
        s->setRange(lo, hi);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setValue(value);
        s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        connect(s, &QDoubleSpinBox::valueChanged, this, update(setter));
        return s;
    };
    auto* weightPx = spin(o.weightPx, 5, 1000, 0, tr(" px per weight"), [](AnalysisOptions& op, double v) { op.weightPx = v; });
    auto* pxPerN = spin(o.pxPerNewton, 1e-6, 1e4, 6, tr(" px/N"), [](AnalysisOptions& op, double v) { op.pxPerNewton = v; });
    vf->addRow(tr("Weight arrow"), weightPx);
    vf->addRow(tr("Absolute scale"), pxPerN);
    auto syncScale = [weightPx, pxPerN](int mode) {
        weightPx->setEnabled(mode == 0);
        pxPerN->setEnabled(mode == 1);
    };
    syncScale(int(o.scaleMode));
    connect(scaleMode, &QComboBox::currentIndexChanged, this, [this, syncScale](int i) {
        AnalysisOptions opts = analysis_->options();
        opts.scaleMode = AnalysisOptions::ScaleMode(i);
        analysis_->setOptions(opts);
        syncScale(i);
    });
    vf->addRow(tr("Velocity scale"),
               spin(o.pxPerMps, 0.1, 1000, 1, tr(" px per m/s"), [](AnalysisOptions& op, double v) { op.pxPerMps = v; }));
    vf->addRow(tr("Acceleration scale"),
               spin(o.pxPerMps2, 0.01, 1000, 2, tr(" px per m/s²"), [](AnalysisOptions& op, double v) { op.pxPerMps2 = v; }));
    lay->addWidget(vectors);

    // Trails.
    auto* trails = new QGroupBox(tr("Trails"), this);
    auto* tl = new QVBoxLayout(trails);
    auto* showTrails = new QCheckBox(tr("Show trails"), trails);
    showTrails->setChecked(o.trails);
    showTrails->setToolTip(tr("Bodies draw a trail when their \"Show trail\" property is on."));
    connect(showTrails, &QCheckBox::toggled, this, update([](AnalysisOptions& op, bool v) { op.trails = v; }));
    tl->addWidget(showTrails);
    auto* enable = new QPushButton(tr("Trail the selected bodies"), trails);
    connect(enable, &QPushButton::clicked, this, [this] {
        std::vector<ItemDef> after;
        for (ItemId id : doc_->selection()) {
            const ItemDef* d = doc_->item(id);
            const int ti = d ? d->type->schema().indexOf(QStringLiteral("trail")) : -1;
            if (ti < 0) continue;
            ItemDef e = *d;
            e.set(ti, true);
            after.push_back(e);
        }
        doc_->editItems(after, tr("Show trails"));
    });
    tl->addWidget(enable);
    auto* clear = new QPushButton(tr("Clear trails"), trails);
    connect(clear, &QPushButton::clicked, analysis_, &Analysis::clearTrails);
    tl->addWidget(clear);
    lay->addWidget(trails);

    // Energy.
    auto* energy = new QGroupBox(tr("Energy"), this);
    auto* el = new QVBoxLayout(energy);
    auto* showEnergy = new QCheckBox(tr("Energy overlay (kinetic, potential, total, drift)"), energy);
    showEnergy->setChecked(o.energy);
    connect(showEnergy, &QCheckBox::toggled, this, update([](AnalysisOptions& op, bool v) { op.energy = v; }));
    el->addWidget(showEnergy);
    lay->addWidget(energy);
    lay->addStretch(1);
}

} // namespace kn
