// Dockable live graph: any quantity against time or another quantity, several
// series, pause, zoom/pan, CSV export.
#pragma once

#include "Quantities.h"

#include <QWidget>

class QAction;
class QToolButton;

namespace kn {

class PlotWidget : public QWidget {
    Q_OBJECT
public:
    struct Series {
        QString name;
        QColor color;
        const std::vector<double>* ys;
        bool visible = true;
    };
    explicit PlotWidget(QWidget* parent = nullptr);
    void setData(const std::vector<double>* xs, std::vector<Series> series, QString xLabel, QString yLabel);
    void refresh();
    void autoRange();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    QRectF plotRect() const;
    void computeAutoRange();
    QPointF toPixel(double x, double y, const QRectF& r) const;

    const std::vector<double>* xs_ = nullptr;
    std::vector<Series> series_;
    QString xLabel_, yLabel_;
    double x0_ = 0, x1_ = 1, y0_ = 0, y1_ = 1;
    bool auto_ = true;
    QPointF dragStart_;
    double dragX0_ = 0, dragX1_ = 0, dragY0_ = 0, dragY1_ = 0;
};

class GraphPanel : public QWidget {
    Q_OBJECT
public:
    GraphPanel(Document* doc, SimController* sim, QWidget* parent = nullptr);
    void addSeries(const Quantity& q);
    void setXAxis(const Quantity& q);
    bool exportCsv(const QString& path, QString* error = nullptr) const;
    int rowCount() const { return int(t_.size()); }

signals:
    void titleChanged(const QString& title);

private:
    void sample();
    void clear();
    void rebind();
    void rebuildSeriesMenu();
    bool chooseQuantity(const QString& title, bool allowTime, Quantity& out);

    struct Series {
        Quantity q;
        QColor color;
        std::vector<double> y;
        bool visible = true;
    };
    Document* doc_;
    SimController* sim_;
    PlotWidget* plot_;
    QToolButton* seriesButton_;
    QAction* pauseAct_;
    Quantity xq_; // Kind::Time by default
    std::vector<double> t_, x_;
    std::vector<Series> series_;
    uint64_t generation_ = ~0ull;
    double lastTime_ = -1.0;
};

} // namespace kn
