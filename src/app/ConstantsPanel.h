// Sliders for the scene's Constant items. Dragging a slider edits the constant
// (one undo step per drag) and, while simulating, every expression that uses
// it changes live.
#pragma once

#include <QWidget>

class QVBoxLayout;
class QLabel;

namespace kn {

class Document;

class ConstantsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ConstantsPanel(Document* doc, QWidget* parent = nullptr);

private:
    void rebuild();
    void refreshValues();

    Document* doc_;
    QWidget* rows_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QLabel* empty_ = nullptr;
    bool updating_ = false;
};

} // namespace kn
