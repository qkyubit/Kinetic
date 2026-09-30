// Settings → Configure Physics: sleeping ("timeout"), world limit and
// integration settings of the current scene, optionally saved as the
// defaults for new scenes.
#pragma once

#include <QDialog>

class QCheckBox;

namespace kn {

class Document;
struct ItemDef;

class PhysicsOptionsDialog : public QDialog {
    Q_OBJECT
public:
    PhysicsOptionsDialog(Document* doc, QWidget* parent = nullptr);
    // Applies the saved defaults (if any) to a new scene's world settings.
    static void applyDefaults(ItemDef& world);
    void accept() override;

private:
    void restoreDefaults();

    Document* doc_;
    struct Row {
        int index;
        QWidget* editor;
    };
    std::vector<Row> rows_;
    QCheckBox* saveDefaults_;
};

} // namespace kn
