#include "TriggerLogPanel.h"

#include "Document.h"
#include "Icons.h"
#include "SimController.h"

#include <QAbstractTableModel>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSaveFile>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

namespace kn {

class TriggerLogModel : public QAbstractTableModel {
public:
    explicit TriggerLogModel(Document* doc, QObject* parent) : QAbstractTableModel(parent), doc_(doc) {}

    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : int(events_.size()); }
    int columnCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : 5; }

    QVariant headerData(int section, Qt::Orientation o, int role) const override
    {
        if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
        static const char* names[] = {"Time (s)", "Step", "Trigger", "Body", "Event"};
        return tr(names[section]);
    }

    QVariant data(const QModelIndex& i, int role) const override
    {
        if (role != Qt::DisplayRole || !i.isValid()) return {};
        return cell(events_[size_t(i.row())], i.column());
    }

    QString cell(const TriggerEvent& e, int column) const
    {
        switch (column) {
        case 0: return QString::number(e.time, 'f', 4);
        case 1: return QString::number(e.step);
        case 2: return name(e.trigger);
        case 3: return name(e.body);
        default: return e.enter ? tr("enter") : tr("leave");
        }
    }

    // Events re-simulated after scrubbing back (or after an edit branched the
    // timeline) replace the logged ones from that step on.
    void append(const std::vector<TriggerEvent>& in)
    {
        for (const TriggerEvent& e : in) {
            if (!events_.empty() && events_.back().step >= e.step) {
                const auto first = std::lower_bound(events_.begin(), events_.end(), e.step,
                                                    [](const TriggerEvent& a, uint64_t s) { return a.step < s; });
                const int from = int(first - events_.begin());
                beginRemoveRows({}, from, int(events_.size()) - 1);
                events_.erase(first, events_.end());
                endRemoveRows();
            }
            beginInsertRows({}, int(events_.size()), int(events_.size()));
            events_.push_back(e);
            endInsertRows();
        }
    }

    void clear()
    {
        beginResetModel();
        events_.clear();
        endResetModel();
    }

    const std::vector<TriggerEvent>& events() const { return events_; }

private:
    QString name(ItemId id) const
    {
        const ItemDef* d = doc_->item(id);
        return d ? d->name : QStringLiteral("#%1").arg(id);
    }

    Document* doc_;
    std::vector<TriggerEvent> events_;
};

TriggerLogPanel::TriggerLogPanel(Document* doc, SimController* sim, QWidget* parent)
    : QWidget(parent), sim_(sim), model_(new TriggerLogModel(doc, this))
{
    auto* bar = new QToolBar(this);
    bar->setIconSize(QSize(16, 16));
    bar->addAction(themedIcon(QStringLiteral("edit-clear-history"), QStringLiteral(":/icons/stop.svg")), tr("Clear"),
                   this, &TriggerLogPanel::clear);
    bar->addAction(themedIcon(QStringLiteral("document-export"), QStringLiteral(":/icons/tool.svg")),
                   tr("Export CSV…"), this, &TriggerLogPanel::exportDialog);
    count_ = new QLabel(this);
    bar->addWidget(count_);

    view_ = new QTableView(this);
    view_->setModel(model_);
    view_->verticalHeader()->hide();
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setAlternatingRowColors(true);
    view_->verticalHeader()->setDefaultSectionSize(view_->fontMetrics().height() + 6);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(bar);
    l->addWidget(view_);

    auto updateCount = [this] { count_->setText(tr("  %n event(s)", nullptr, model_->rowCount())); };
    connect(sim, &SimController::triggerEvents, this, [this, updateCount](const std::vector<TriggerEvent>& e) {
        model_->append(e);
        updateCount();
        view_->scrollToBottom();
    });
    connect(model_, &QAbstractItemModel::modelReset, this, updateCount);
    // A new simulation run starts a new log.
    connect(sim, &SimController::stateChanged, this, [this] {
        if (sim_->active() && !wasActive_) clear();
        wasActive_ = sim_->active();
    });
    updateCount();
}

int TriggerLogPanel::rowCount() const { return model_->rowCount(); }

void TriggerLogPanel::clear() { model_->clear(); }

bool TriggerLogPanel::exportCsv(const QString& path, QString* error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QByteArray out = "time_s,step,trigger,body,event\n";
    auto quoted = [](QString s) { return QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'); };
    for (const TriggerEvent& e : model_->events()) {
        QStringList row;
        for (int c = 0; c < 5; ++c) row << (c == 2 || c == 3 ? quoted(model_->cell(e, c)) : model_->cell(e, c));
        out += row.join(QLatin1Char(',')).toUtf8() + '\n';
    }
    if (f.write(out) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void TriggerLogPanel::exportDialog()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Trigger Log"), QStringLiteral("triggers.csv"),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;
    QString err;
    if (!exportCsv(path, &err)) QMessageBox::warning(this, tr("Cannot export"), tr("%1: %2").arg(path, err));
}

} // namespace kn
