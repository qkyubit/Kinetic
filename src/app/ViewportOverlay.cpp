#include "ViewportOverlay.h"

#include "Viewport.h"

#include <QPainter>
#include <QPainterPath>

namespace kn {

ViewportOverlay::ViewportOverlay(Viewport* viewport) : QWidget(viewport), vp_(viewport)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

static void drawTextWithHalo(QPainter& p, QPointF pos, const QString& text, const QColor& color, const QColor& halo)
{
    QPainterPath path;
    path.addText(pos, p.font(), text);
    p.setPen(QPen(halo, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
}

void ViewportOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& th = vp_->theme();
    QColor halo(th.background.r, th.background.g, th.background.b, 200);
    const QFontMetricsF fm(font());

    // Labels produced by item drawing and measuring tools.
    QString buf;
    for (const TextLabel& l : vp_->batch().labels) {
        buf = l.text;
        if (l.hasValue) {
            buf += QString::number(l.value, 'f', l.decimals);
            buf += l.unit;
        }
        const QPointF s = vp_->toScreen(l.pos) + QPointF(l.dx, l.dy);
        const double w = fm.horizontalAdvance(buf);
        drawTextWithHalo(p, s - QPointF(w * 0.5, -fm.ascent() * 0.35), buf,
                         QColor(l.color.r, l.color.g, l.color.b, l.color.a), halo);
    }

    // Scale bar (bottom left): a 1-2-5 length close to 100 px.
    const double ppm = vp_->pixelsPerMetre();
    const double target = 100.0 / ppm;
    const double p10 = std::pow(10.0, std::floor(std::log10(target)));
    double len = p10;
    for (double m : {1.0, 2.0, 5.0})
        if (p10 * m <= target) len = p10 * m;
    const double barPx = len * ppm;
    const QPointF o(12, height() - 14);
    const QColor tc(th.text.r, th.text.g, th.text.b);
    p.setPen(QPen(tc, 2));
    p.drawLine(o, o + QPointF(barPx, 0));
    p.drawLine(o + QPointF(0, -4), o + QPointF(0, 4));
    p.drawLine(o + QPointF(barPx, -4), o + QPointF(barPx, 4));
    const QString scaleText = len >= 1.0 ? QStringLiteral("%1 m").arg(len) : QStringLiteral("%1 cm").arg(len * 100.0);
    drawTextWithHalo(p, o + QPointF(barPx + 6, fm.ascent() * 0.35), scaleText + QStringLiteral("   (%1 px/m)").arg(ppm, 0, 'f', 1),
                     tc, halo);

    // Tool hint (top centre).
    const QString hint = vp_->statusHint();
    if (!hint.isEmpty()) {
        const double w = fm.horizontalAdvance(hint);
        drawTextWithHalo(p, QPointF((width() - w) * 0.5, 8 + fm.ascent()), hint, tc, halo);
    }

    for (auto& fn : painters_) fn(p);
}

} // namespace kn
