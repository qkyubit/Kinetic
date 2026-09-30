#include "PhysicsOptionsDialog.h"

#include "Document.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kn {

namespace {
// Shown in this order, in these sections.
const std::vector<std::pair<QString, QStringList>> kSections = {
    {QStringLiteral("Sleeping"),
     {QStringLiteral("sleep"), QStringLiteral("sleepDelay"), QStringLiteral("fieldSleepDelay"), QStringLiteral("sleepThreshold")}},
    {QStringLiteral("World"), {QStringLiteral("worldLimit")}},
    {QStringLiteral("Integration"),
     {QStringLiteral("physicsHz"), QStringLiteral("substeps"), QStringLiteral("continuous")}},
};

const QString kDefaultsGroup = QStringLiteral("physicsDefaults");

PropValue editorValue(const PropDef& pd, QWidget* w)
{
    if (auto* c = qobject_cast<QCheckBox*>(w)) return c->isChecked();
    if (auto* i = qobject_cast<QSpinBox*>(w)) return int64_t(i->value());
    return qobject_cast<QDoubleSpinBox*>(w)->value() / pd.scale;
}

void setEditor(const PropDef& pd, QWidget* w, const PropValue& v)
{
    if (auto* c = qobject_cast<QCheckBox*>(w)) c->setChecked(std::get<bool>(v));
    else if (auto* i = qobject_cast<QSpinBox*>(w)) i->setValue(int(std::get<int64_t>(v)));
    else qobject_cast<QDoubleSpinBox*>(w)->setValue(std::get<double>(v) * pd.scale);
}
} // namespace

PhysicsOptionsDialog::PhysicsOptionsDialog(Document* doc, QWidget* parent) : QDialog(parent), doc_(doc)
{
    setWindowTitle(tr("Configure Physics"));
    auto* lay = new QVBoxLayout(this);
    const ItemDef& world = doc_->world();
    const PropertySchema& sch = world.type->schema();
    for (const auto& [title, keys] : kSections) {
        auto* box = new QGroupBox(tr(qPrintable(title)), this);
        auto* form = new QFormLayout(box);
        for (const QString& key : keys) {
            const int i = sch.indexOf(key);
            if (i < 0) continue;
            const PropDef& pd = sch[i];
            QWidget* ed = nullptr;
            if (pd.type == PropType::Bool) {
                ed = new QCheckBox(box);
            } else if (pd.type == PropType::Int) {
                auto* s = new QSpinBox(box);
                s->setRange(int(pd.min), int(pd.max));
                ed = s;
            } else {
                auto* s = new QDoubleSpinBox(box);
                s->setRange(pd.min * pd.scale, pd.max * pd.scale);
                s->setDecimals(pd.decimals);
                s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
                if (!pd.unit.isEmpty()) s->setSuffix(QLatin1Char(' ') + pd.unit);
                ed = s;
            }
            ed->setToolTip(pd.tooltip);
            ed->setObjectName(key);
            setEditor(pd, ed, world.props[size_t(i)]);
            auto* label = new QLabel(pd.label, box);
            label->setToolTip(pd.tooltip);
            form->addRow(label, ed);
            rows_.push_back({i, ed});
        }
        lay->addWidget(box);
    }
    auto* note = new QLabel(tr("A body entering or leaving a wind, vortex or other field region wakes every body in "
                               "it and restarts their delay."),
                            this);
    note->setWordWrap(true);
    lay->addWidget(note);
    saveDefaults_ = new QCheckBox(tr("Also use these settings for new scenes"), this);
    lay->addWidget(saveDefaults_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &PhysicsOptionsDialog::restoreDefaults);
    lay->addWidget(buttons);
}

void PhysicsOptionsDialog::restoreDefaults()
{
    const PropertySchema& sch = doc_->world().type->schema();
    for (const Row& r : rows_) setEditor(sch[r.index], r.editor, sch[r.index].def);
}

void PhysicsOptionsDialog::accept()
{
    const ItemDef& world = doc_->world();
    const PropertySchema& sch = world.type->schema();
    doc_->undoStack()->beginMacro(tr("Physics options"));
    for (const Row& r : rows_) doc_->setWorldProperty(r.index, editorValue(sch[r.index], r.editor));
    doc_->undoStack()->endMacro();
    if (saveDefaults_->isChecked()) {
        QSettings s;
        s.beginGroup(kDefaultsGroup);
        for (const Row& r : rows_) s.setValue(sch[r.index].key, valueToJson(sch[r.index], doc_->world().props[size_t(r.index)]).toVariant());
    }
    QDialog::accept();
}

void PhysicsOptionsDialog::applyDefaults(ItemDef& world)
{
    QSettings s;
    s.beginGroup(kDefaultsGroup);
    const PropertySchema& sch = world.type->schema();
    for (const QString& key : s.childKeys()) {
        const int i = sch.indexOf(key);
        if (i < 0) continue;
        PropValue v;
        if (valueFromJson(sch[i], QJsonValue::fromVariant(s.value(key)), v)) world.set(i, clampValue(sch[i], v));
    }
}

} // namespace kn
