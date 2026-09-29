// Property editor generated from the selected items' schema. Every numeric
// property gets a slider and a spin box with its unit; every edit is undoable.
#pragma once

#include "Document.h"
#include "SimController.h"

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace kn {

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(Document* doc, SimController* sim, QWidget* parent = nullptr);

private:
    struct Binding {
        int index;
        QWidget* editor;
        std::function<void(const PropValue&)> show;
    };

    void rebuild();
    void refresh();
    void updateEnabled();
    const ItemDef* firstTarget() const;
    void edit(int index, const PropValue& v, bool continuous);
    QWidget* makeEditor(const PropDef& pd, int index, Binding& b);

    Document* doc_;
    SimController* sim_;
    QScrollArea* scroll_;
    QWidget* content_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* typeLabel_ = nullptr;
    std::vector<Binding> bindings_;
    QList<ItemId> targets_;
    const ItemType* type_ = nullptr;
    bool worldMode_ = false;
};

} // namespace kn
