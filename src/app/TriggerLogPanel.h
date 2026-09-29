// Table of trigger-zone enter/leave events with CSV export.
#pragma once

#include "core/Snapshot.h"

#include <QWidget>

class QTableView;
class QLabel;

namespace kn {

class Document;
class SimController;
class TriggerLogModel;

class TriggerLogPanel : public QWidget {
    Q_OBJECT
public:
    TriggerLogPanel(Document* doc, SimController* sim, QWidget* parent = nullptr);
    int rowCount() const;
    bool exportCsv(const QString& path, QString* error = nullptr) const;
    void clear();

private:
    void exportDialog();

    SimController* sim_;
    TriggerLogModel* model_;
    QTableView* view_;
    QLabel* count_;
    bool wasActive_ = false;
};

} // namespace kn
