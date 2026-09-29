#include "GraphPanel.h"

#include "Icons.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace kn {

namespace {
constexpr size_t kMaxRows = 200000;
const QColor kSeriesColors[] = {QColor(0x3d, 0xae, 0xe9), QColor(0xf6, 0x74, 0x00), QColor(0x27, 0xae, 0x60),
                                QColor(0xda, 0x44, 0x53), QColor(0x9b, 0x59, 0xb6), QColor(0xf1, 0xc4, 0x0f),
                                QColor(0x1a, 0xbc, 0x9c), QColor(0x7f, 0x8c, 0x8d)};

// 1-2-5 tick positions covering [lo, hi].
std::vector<double> niceTicks(double lo, double hi, int maxTicks)
{
    std::vector<double> t;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return t;
    const double raw = (hi - lo) / std::max(maxTicks, 2);
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    double step = p * 10;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (p * m >= raw) {
            step = p * m;
            break;
        }
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9 && t.size() < 100; v += step)
        t.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
    return t;
}

QString tickText(double v)
{
    const double a = std::abs(v);
    if (a != 0 && (a >= 1e5 || a < 1e-3)) return QString::number(v, 'g', 3);
    return QString::number(v, 'g', 6);
}
} // namespace

// --- plot ---------------------------------------------------------------------------------

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(240, 160);
    setMouseTracking(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PlotWidget::setData(const std::vector<double>* xs, std::vector<Series> series, QString xLabel, QString yLabel)
{
    xs_ = xs;
    series_ = std::move(series);
    xLabel_ = std::move(xLabel);
    yLabel_ = std::move(yLabel);
    refresh();
}

void PlotWidget::refresh()
{
    if (auto_) computeAutoRange();
    update();
}

void PlotWidget::autoRange()
{
    auto_ = true;
    refresh();
}

void PlotWidget::computeAutoRange()
{
    double xmin = std::numeric_limits<double>::max(), xmax = -xmin, ymin = xmin, ymax = -xmin;
    if (xs_) {
        for (const Series& s : series_) {
            if (!s.visible) continue;
            const size_t n = std::min(xs_->size(), s.ys->size());
            for (size_t i = 0; i < n; ++i) {
                const double x = (*xs_)[i], y = (*s.ys)[i];
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                xmin = std::min(xmin, x);
                xmax = std::max(xmax, x);
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
            }
        }
    }
    if (xmin > xmax) {
        x0_ = 0, x1_ = 1, y0_ = -1, y1_ = 1;
        return;
    }
    if (xmax - xmin < 1e-12) xmax = xmin + 1.0;
    if (ymax - ymin < 1e-12 * std::max(1.0, std::abs(ymax))) {
        const double pad = std::max(std::abs(ymax) * 0.05, 1e-6);
        ymin -= pad;
        ymax += pad;
    }
    const double py = (ymax - ymin) * 0.06;
    x0_ = xmin;
    x1_ = xmax;
    y0_ = ymin - py;
    y1_ = ymax + py;
}

QRectF PlotWidget::plotRect() const
{
    const QFontMetricsF fm(font());
    const double left = fm.horizontalAdvance(QStringLiteral("-0.000000")) + 14;
    return QRectF(left, 8, std::max(10.0, width() - left - 10), std::max(10.0, height() - 8 - fm.height() * 2.6));
}

QPointF PlotWidget::toPixel(double x, double y, const QRectF& r) const
{
    return {r.left() + (x - x0_) / (x1_ - x0_) * r.width(), r.bottom() - (y - y0_) / (y1_ - y0_) * r.height()};
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    const QFontMetricsF fm(font());
    const QColor text = palette().color(QPalette::Text);
    QColor grid = text;
    grid.setAlpha(35);

    // Grid and tick labels.
    p.setPen(QPen(grid, 1));
    for (double x : niceTicks(x0_, x1_, int(r.width() / 80))) {
        const double px = toPixel(x, 0, r).x();
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        p.setPen(text);
        const QString s = tickText(x);
        p.drawText(QPointF(px - fm.horizontalAdvance(s) / 2, r.bottom() + fm.ascent() + 3), s);
    }
    for (double y : niceTicks(y0_, y1_, int(r.height() / 40))) {
        const double py = toPixel(0, y, r).y();
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(r.left(), py), QPointF(r.right(), py));
        p.setPen(text);
        const QString s = tickText(y);
        p.drawText(QPointF(r.left() - fm.horizontalAdvance(s) - 5, py + fm.ascent() / 2 - 1), s);
    }
    p.setPen(QPen(text, 1));
    p.drawRect(r);
    p.drawText(QPointF(r.center().x() - fm.horizontalAdvance(xLabel_) / 2, height() - 4), xLabel_);

    // Series (decimated to about two points per pixel column).
    p.setClipRect(r);
    if (xs_) {
        for (const Series& s : series_) {
            if (!s.visible) continue;
            const size_t n = std::min(xs_->size(), s.ys->size());
            QPolygonF line;
            const size_t stride = std::max<size_t>(1, n / size_t(std::max(1.0, r.width() * 2)));
            auto flush = [&] {
                if (line.size() > 1) p.drawPolyline(line);
                line.clear();
            };
            p.setPen(QPen(s.color, 1.6));
            for (size_t i = 0; i < n; i += stride) {
                const double x = (*xs_)[i], y = (*s.ys)[i];
                if (!std::isfinite(x) || !std::isfinite(y)) {
                    flush();
                    continue;
                }
                line << toPixel(x, y, r);
            }
            if (n > 0 && (n - 1) % stride) { // always include the newest point
                const double x = (*xs_)[n - 1], y = (*s.ys)[n - 1];
                if (std::isfinite(x) && std::isfinite(y)) line << toPixel(x, y, r);
            }
            flush();
        }
    }
    p.setClipping(false);

    // Legend.
    double ly = r.top() + 6;
    for (const Series& s : series_) {
        QColor bg = palette().color(QPalette::Base);
        bg.setAlpha(200);
        const double w = fm.horizontalAdvance(s.name) + 26;
        p.fillRect(QRectF(r.left() + 6, ly - 2, w, fm.height() + 2), bg);
        p.setPen(QPen(s.color, 3));
        p.drawLine(QPointF(r.left() + 10, ly + fm.height() / 2), QPointF(r.left() + 24, ly + fm.height() / 2));
        QColor tc = text;
        if (!s.visible) tc.setAlpha(110);
        p.setPen(tc);
        p.drawText(QPointF(r.left() + 28, ly + fm.ascent()), s.name);
        ly += fm.height() + 2;
    }
    if (series_.empty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        const QString hint = tr("Add a series with the + button");
        p.drawText(QPointF(r.center().x() - fm.horizontalAdvance(hint) / 2, r.center().y()), hint);
    }
}

void PlotWidget::wheelEvent(QWheelEvent* e)
{
    const QRectF r = plotRect();
    const double f = std::pow(0.85, e->angleDelta().y() / 120.0);
    const QPointF pos = e->position();
    const double cx = x0_ + (pos.x() - r.left()) / r.width() * (x1_ - x0_);
    const double cy = y0_ + (r.bottom() - pos.y()) / r.height() * (y1_ - y0_);
    const bool onlyY = e->modifiers() & Qt::ShiftModifier;
    const bool onlyX = e->modifiers() & Qt::ControlModifier;
    if (!onlyY) {
        x0_ = cx + (x0_ - cx) * f;
        x1_ = cx + (x1_ - cx) * f;
    }
    if (!onlyX) {
        y0_ = cy + (y0_ - cy) * f;
        y1_ = cy + (y1_ - cy) * f;
    }
    auto_ = false;
    update();
}

void PlotWidget::mousePressEvent(QMouseEvent* e)
{
    dragStart_ = e->position();
    dragX0_ = x0_, dragX1_ = x1_, dragY0_ = y0_, dragY1_ = y1_;
}

void PlotWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QRectF r = plotRect();
    const double dx = (e->position().x() - dragStart_.x()) / r.width() * (dragX1_ - dragX0_);
    const double dy = (e->position().y() - dragStart_.y()) / r.height() * (dragY1_ - dragY0_);
    x0_ = dragX0_ - dx;
    x1_ = dragX1_ - dx;
    y0_ = dragY0_ + dy;
    y1_ = dragY1_ + dy;
    auto_ = false;
    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent*) { autoRange(); }

void PlotWidget::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) update();
    QWidget::changeEvent(e);
}

// --- panel ---------------------------------------------------------------------------------------

GraphPanel::GraphPanel(Document* doc, SimController* sim, QWidget* parent) : QWidget(parent), doc_(doc), sim_(sim)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* bar = new QToolBar(this);
    bar->setIconSize(QSize(16, 16));
    const QString fb = QStringLiteral(":/icons/tool.svg");
    QAction* add = bar->addAction(themedIcon(QStringLiteral("list-add"), fb), tr("Add Series…"));
    seriesButton_ = new QToolButton(this);
    seriesButton_->setText(tr("Series"));
    seriesButton_->setPopupMode(QToolButton::InstantPopup);
    seriesButton_->setMenu(new QMenu(seriesButton_));
    bar->addWidget(seriesButton_);
    QAction* xAxis = bar->addAction(themedIcon(QStringLiteral("labplot-xy-curve"), fb), tr("X Axis…"));
    bar->addSeparator();
    pauseAct_ = bar->addAction(themedIcon(QStringLiteral("media-playback-pause"), QStringLiteral(":/icons/pause.svg")),
                               tr("Pause Graph"));
    pauseAct_->setCheckable(true);
    pauseAct_->setToolTip(tr("Freeze the graph (the simulation keeps running)"));
    QAction* fit = bar->addAction(themedIcon(QStringLiteral("zoom-fit-best"), fb), tr("Auto Range"));
    fit->setToolTip(tr("Fit all data (double-click the plot). Wheel zooms, Shift/Ctrl+wheel zoom one axis, drag pans."));
    QAction* clearAct = bar->addAction(themedIcon(QStringLiteral("edit-clear"), fb), tr("Clear"));
    QAction* csv = bar->addAction(themedIcon(QStringLiteral("document-export"), fb), tr("Export CSV…"));
    lay->addWidget(bar);
    plot_ = new PlotWidget(this);
    lay->addWidget(plot_, 1);

    connect(add, &QAction::triggered, this, [this] {
        Quantity q;
        if (chooseQuantity(tr("Add Series"), false, q)) addSeries(q);
    });
    connect(xAxis, &QAction::triggered, this, [this] {
        Quantity q = xq_;
        if (chooseQuantity(tr("X Axis"), true, q)) setXAxis(q);
    });
    connect(fit, &QAction::triggered, plot_, &PlotWidget::autoRange);
    connect(clearAct, &QAction::triggered, this, &GraphPanel::clear);
    connect(csv, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QString(), tr("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        QString err;
        if (!exportCsv(path, &err)) QMessageBox::warning(this, tr("Export failed"), err);
    });
    connect(sim_, &SimController::snapshotUpdated, this, &GraphPanel::sample);
    connect(doc_, &Document::sceneReset, this, [this] {
        series_.clear();
        xq_ = {};
        clear();
    });
    rebind();
}

bool GraphPanel::chooseQuantity(const QString& title, bool allowTime, Quantity& out)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto* form = new QFormLayout(&dlg);
    auto* object = new QComboBox(&dlg);
    auto* quantity = new QComboBox(&dlg);
    object->addItem(themedIcon(QStringLiteral("applications-science"), QStringLiteral(":/icons/tool.svg")),
                    tr("Scene (time, energy)"), QVariant::fromValue<qulonglong>(0));
    for (const ItemDef& d : doc_->scene().items) {
        const ItemKind k = d.type->kind();
        if (k == ItemKind::Group) continue;
        object->addItem(itemIcon(d.type), d.name, QVariant::fromValue<qulonglong>(d.id));
    }
    const ItemId preselect = out.item ? out.item : (doc_->selection().isEmpty() ? 0 : doc_->selection().first());
    object->setCurrentIndex(std::max(0, object->findData(QVariant::fromValue<qulonglong>(preselect))));
    QList<Quantity> options;
    auto fill = [&] {
        quantity->clear();
        options = Quantity::options(*doc_, ItemId(object->currentData().toULongLong()));
        if (!allowTime) options.removeIf([](const Quantity& q) { return q.kind == Quantity::Kind::Time; });
        for (const Quantity& q : options) {
            const QString u = q.unit(*doc_);
            quantity->addItem(u.isEmpty() ? q.name(*doc_) : QStringLiteral("%1 (%2)").arg(q.name(*doc_), u));
        }
        const int cur = int(options.indexOf(out));
        if (cur >= 0) quantity->setCurrentIndex(cur);
    };
    fill();
    connect(object, &QComboBox::currentIndexChanged, &dlg, fill);
    form->addRow(tr("Object"), object);
    form->addRow(tr("Quantity"), quantity);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted || quantity->currentIndex() < 0 || quantity->currentIndex() >= options.size())
        return false;
    out = options[quantity->currentIndex()];
    return true;
}

void GraphPanel::addSeries(const Quantity& q)
{
    Series s;
    s.q = q;
    s.color = kSeriesColors[series_.size() % std::size(kSeriesColors)];
    s.y.assign(t_.size(), std::numeric_limits<double>::quiet_NaN());
    series_.push_back(std::move(s));
    rebind();
}

void GraphPanel::setXAxis(const Quantity& q)
{
    xq_ = q;
    clear();
}

void GraphPanel::clear()
{
    t_.clear();
    x_.clear();
    for (Series& s : series_) s.y.clear();
    lastTime_ = -1.0;
    rebind();
    plot_->autoRange();
}

void GraphPanel::rebind()
{
    std::vector<PlotWidget::Series> ps;
    QStringList names;
    for (const Series& s : series_) {
        const QString u = s.q.unit(*doc_);
        const QString name = u.isEmpty() ? s.q.label(*doc_) : QStringLiteral("%1 (%2)").arg(s.q.label(*doc_), u);
        ps.push_back({name, s.color, &s.y, s.visible});
        names << s.q.name(*doc_);
    }
    const bool timeAxis = xq_.kind == Quantity::Kind::Time;
    const QString xu = xq_.unit(*doc_);
    plot_->setData(timeAxis ? &t_ : &x_, std::move(ps),
                   timeAxis ? tr("time (s)") : QStringLiteral("%1 (%2)").arg(xq_.label(*doc_), xu), QString());
    rebuildSeriesMenu();
    emit titleChanged(names.isEmpty() ? tr("Graph") : tr("Graph: %1").arg(names.join(QStringLiteral(", "))));
}

void GraphPanel::rebuildSeriesMenu()
{
    QMenu* m = seriesButton_->menu();
    m->clear();
    for (size_t i = 0; i < series_.size(); ++i) {
        QMenu* sub = m->addMenu(series_[i].q.label(*doc_));
        QAction* vis = sub->addAction(tr("Visible"));
        vis->setCheckable(true);
        vis->setChecked(series_[i].visible);
        connect(vis, &QAction::toggled, this, [this, i](bool on) {
            if (i < series_.size()) series_[i].visible = on;
            rebind();
        });
        connect(sub->addAction(tr("Remove")), &QAction::triggered, this, [this, i] {
            if (i < series_.size()) series_.erase(series_.begin() + long(i));
            rebind();
        });
    }
    seriesButton_->setEnabled(!series_.empty());
}

void GraphPanel::sample()
{
    if (!sim_->live() || pauseAct_->isChecked()) return;
    const Snapshot& s = sim_->snapshot();
    if (s.generation != generation_) {
        generation_ = s.generation;
        t_.clear();
        x_.clear();
        for (Series& se : series_) se.y.clear();
        lastTime_ = -1.0;
    }
    const double t = s.time;
    if (t < lastTime_ - 1e-9) {
        // Rewound or scrubbed back: drop the future, it will be re-recorded.
        const size_t keep = size_t(std::lower_bound(t_.begin(), t_.end(), t + 1e-9) - t_.begin());
        t_.resize(keep);
        x_.resize(std::min(x_.size(), keep));
        for (Series& se : series_) se.y.resize(std::min(se.y.size(), keep));
    } else if (t <= lastTime_ + 1e-12) {
        return; // no new frame
    }
    lastTime_ = t;
    if (t_.size() >= kMaxRows) {
        const long drop = long(kMaxRows / 2);
        t_.erase(t_.begin(), t_.begin() + drop);
        x_.erase(x_.begin(), x_.begin() + std::min<long>(drop, long(x_.size())));
        for (Series& se : series_) se.y.erase(se.y.begin(), se.y.begin() + std::min<long>(drop, long(se.y.size())));
    }
    t_.push_back(t);
    double xv = t;
    if (xq_.kind != Quantity::Kind::Time && !xq_.eval(*sim_, *doc_, xv)) xv = std::numeric_limits<double>::quiet_NaN();
    x_.push_back(xv);
    for (Series& se : series_) {
        double v;
        se.y.push_back(se.q.eval(*sim_, *doc_, v) ? v : std::numeric_limits<double>::quiet_NaN());
    }
    plot_->refresh();
}

bool GraphPanel::exportCsv(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    auto quote = [](QString s) { return QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'); };
    QStringList header{QStringLiteral("time (s)")};
    const bool timeAxis = xq_.kind == Quantity::Kind::Time;
    if (!timeAxis) header << quote(QStringLiteral("%1 (%2)").arg(xq_.label(*doc_), xq_.unit(*doc_)));
    for (const Series& s : series_) header << quote(QStringLiteral("%1 (%2)").arg(s.q.label(*doc_), s.q.unit(*doc_)));
    out << header.join(QLatin1Char(',')) << '\n';
    auto num = [](double v) { return std::isfinite(v) ? QString::number(v, 'g', 12) : QString(); };
    for (size_t i = 0; i < t_.size(); ++i) {
        QStringList row{num(t_[i])};
        if (!timeAxis) row << num(i < x_.size() ? x_[i] : NAN);
        for (const Series& s : series_) row << num(i < s.y.size() ? s.y[i] : NAN);
        out << row.join(QLatin1Char(',')) << '\n';
    }
    return true;
}

} // namespace kn
