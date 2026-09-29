// Text layer on top of the RHI viewport (labels, readouts, scale bar, HUD).
// Uses QPainter, as allowed for text; geometry stays in the RHI batches.
#pragma once

#include <QWidget>

#include <functional>

namespace kn {

class Viewport;

class ViewportOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ViewportOverlay(Viewport* viewport);
    // Extra painters (energy HUD, probe readouts...) registered by other modules.
    void addPainter(std::function<void(QPainter&)> fn) { painters_.push_back(std::move(fn)); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Viewport* vp_;
    std::vector<std::function<void(QPainter&)>> painters_;
};

} // namespace kn
