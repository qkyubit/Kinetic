#include "Viewport.h"

#include "ViewportOverlay.h"

#include "core/AllocCounter.h"
#include "core/Geometry.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>

namespace kn {

namespace {
constexpr const char* kItemMime = "application/x-kinetic-itemtype";
constexpr double kGizmoPx = 70.0;
constexpr double kHandlePx = 7.0;

struct Ubuf {
    float mvp[16];
    float viewport[2];
    float pxPerM;
    float dpr;
};

QShader loadShader(const char* name)
{
    QFile f(QStringLiteral(":/shaders/%1.qsb").arg(QLatin1String(name)));
    if (!f.open(QIODevice::ReadOnly)) qFatal("missing shader %s", name);
    return QShader::fromSerialized(f.readAll());
}

Color toColor(const QColor& c, int alpha = -1)
{
    return Color{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(alpha >= 0 ? alpha : c.alpha())};
}
} // namespace

Viewport::Viewport(Document* doc, SimController* sim, QWidget* parent) : QRhiWidget(parent), doc_(doc), sim_(sim)
{
    setSampleCount(4);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumSize(200, 150);
    overlay_ = new ViewportOverlay(this);
    updateTheme();

    auto refresh = [this] { update(); };
    connect(doc_, &Document::itemAdded, this, refresh);
    connect(doc_, &Document::itemRemoved, this, refresh);
    connect(doc_, &Document::itemChanged, this, refresh);
    connect(doc_, &Document::orderChanged, this, refresh);
    connect(doc_, &Document::worldChanged, this, refresh);
    connect(doc_, &Document::selectionChanged, this, [this] {
        selected_.clear();
        for (ItemId id : doc_->selection()) selected_.insert(id);
        update();
    });
    connect(doc_, &Document::sceneReset, this, [this] {
        cancelTool();
        setCamera(doc_->scene().view.center, doc_->scene().view.pixelsPerMetre);
    });
    connect(sim_, &SimController::snapshotUpdated, this, refresh);
    connect(sim_, &SimController::stateChanged, this, refresh);
}

Viewport::~Viewport() = default;

// --- scene view ----------------------------------------------------------------------------

const ItemDef* Viewport::def(ItemId id) const { return doc_->item(id); }

void Viewport::ensureSnapIndex() const
{
    const Snapshot& s = sim_->snapshot();
    // Buffers rotate, but with an unchanged structure the ordering is the same.
    if (s.structureVersion == indexedVersion_ && s.generation == indexedGeneration_) return;
    indexedSnap_ = &s;
    indexedVersion_ = s.structureVersion;
    indexedGeneration_ = s.generation;
    auto rebuild = [](std::vector<int>& index, const auto& vec) {
        std::fill(index.begin(), index.end(), -1);
        for (size_t i = 0; i < vec.size(); ++i) {
            const ItemId id = vec[i].id;
            if (id >= index.size()) index.resize(size_t(id) * 2 + 64, -1); // grows only with the scene
            index[id] = int(i);
        }
    };
    rebuild(bodyIndex_, s.bodies);
    rebuild(itemIndex_, s.items);
}

const BodySnap* Viewport::bodySnap(ItemId id) const
{
    if (!sim_->live()) return nullptr;
    ensureSnapIndex();
    if (id >= bodyIndex_.size() || bodyIndex_[id] < 0) return nullptr;
    const auto& b = sim_->snapshot().bodies;
    const size_t i = size_t(bodyIndex_[id]);
    return i < b.size() && b[i].id == id ? &b[i] : nullptr;
}

const ItemSnap* Viewport::itemSnap(ItemId id) const
{
    if (!sim_->live()) return nullptr;
    ensureSnapIndex();
    if (id >= itemIndex_.size() || itemIndex_[id] < 0) return nullptr;
    const auto& v = sim_->snapshot().items;
    const size_t i = size_t(itemIndex_[id]);
    return i < v.size() && v[i].id == id ? &v[i] : nullptr;
}

bool Viewport::pose(ItemId id, Pose& out) const
{
    if (const BodySnap* b = bodySnap(id)) {
        out = b->pose();
        return true;
    }
    const ItemDef* d = doc_->item(id);
    if (!d || !d->type->hasPose()) return false;
    out = d->type->pose(*d);
    return true;
}

// --- camera ------------------------------------------------------------------------------------

void Viewport::setCamera(Vec2 c, double ppm)
{
    center_ = c;
    ppm_ = std::clamp(ppm, 0.5, 5000.0);
    doc_->view().center = center_;
    doc_->view().pixelsPerMetre = ppm_;
    emit viewChanged();
    update();
}

Vec2 Viewport::toWorld(QPointF p) const
{
    return {center_.x + (p.x() - width() * 0.5) / ppm_, center_.y - (p.y() - height() * 0.5) / ppm_};
}

QPointF Viewport::toScreen(Vec2 w) const
{
    return {(w.x - center_.x) * ppm_ + width() * 0.5, height() * 0.5 - (w.y - center_.y) * ppm_};
}

void Viewport::zoomBy(double factor, QPointF anchor)
{
    const Vec2 before = toWorld(anchor);
    const double ppm = std::clamp(ppm_ * factor, 0.5, 5000.0);
    // Keep the world point under the cursor fixed.
    const Vec2 c{before.x - (anchor.x() - width() * 0.5) / ppm, before.y + (anchor.y() - height() * 0.5) / ppm};
    setCamera(c, ppm);
}

void Viewport::zoomToFit()
{
    Aabb box;
    const QList<ItemId> sel = doc_->selection();
    for (const ItemDef& d : doc_->scene().items) {
        if (!sel.isEmpty() && !sel.contains(d.id)) continue;
        if (!doc_->effectiveVisible(d)) continue;
        box.add(d.type->bounds(d, *this));
    }
    if (!box.valid()) {
        setCamera({0, 4}, 50);
        return;
    }
    box.inflate(0.5);
    const Vec2 size = box.size();
    const double ppm = std::min(width() / std::max(size.x, 1e-3), height() / std::max(size.y, 1e-3)) * 0.9;
    setCamera(box.center(), ppm);
}

double Viewport::gridStep() const
{
    // Smallest 1-2-5 step that is at least 12 px apart.
    const double minWorld = 12.0 / ppm_;
    const double p = std::pow(10.0, std::floor(std::log10(minWorld)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (p * m >= minWorld) return p * m;
    return p * 10.0;
}

Vec2 Viewport::snapPoint(Vec2 w) const
{
    if (!snap_) return w;
    const double s = gridStep();
    return {std::round(w.x / s) * s, std::round(w.y / s) * s};
}

void Viewport::setGridVisible(bool on)
{
    grid_ = on;
    update();
}

void Viewport::setTool(Tool t)
{
    if (t != Tool::Connect) connectType_ = nullptr;
    if (t != Tool::Draw) drawType_ = nullptr;
    tool_ = t;
    connectHasA_ = false;
    stroke_.clear();
    polygonMode_ = false;
    if (t != Tool::Ruler) measure_.ruler = false;
    if (t != Tool::Protractor) {
        measure_.protractor = false;
        measure_.protractorPoints = 0;
    }
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit toolChanged();
    update();
}

void Viewport::setGizmo(Gizmo g)
{
    gizmo_ = g;
    update();
}

void Viewport::beginConnect(const ItemType* type)
{
    setTool(Tool::Connect);
    connectType_ = type;
    emit toolChanged();
}

void Viewport::beginDraw(const ItemType* type)
{
    setTool(Tool::Draw);
    drawType_ = type;
    emit toolChanged();
}

void Viewport::cancelTool()
{
    drag_ = Drag::None;
    if (tool_ != Tool::Select) setTool(Tool::Select);
    update();
}

void Viewport::placeItem(const ItemType* type, Vec2 at)
{
    if (!type) return;
    if (type->meta().create == CreateMode::Connect) {
        beginConnect(type);
        return;
    }
    ItemDef d = type->makeDefault(doc_->allocateId(), snapPoint(at));
    doc_->addItems({d}, tr("Add %1").arg(type->meta().name));
}

QString Viewport::statusHint() const
{
    switch (tool_) {
    case Tool::Select: return {};
    case Tool::Connect:
        return connectHasA_ ? tr("%1: click the second body or a point (Esc to cancel)").arg(connectType_->meta().name)
                            : tr("%1: click the first body or a point (Esc to cancel)").arg(connectType_->meta().name);
    case Tool::Draw:
        return polygonMode_ ? tr("Click to add vertices; double-click or Enter to finish, Esc to cancel")
                            : tr("Drag to draw an outline, or click to place polygon vertices (Esc to cancel)");
    case Tool::Ruler: return tr("Ruler: drag to measure a distance (Esc to finish)");
    case Tool::Protractor: return tr("Protractor: click the vertex, then one point on each arm (Esc to finish)");
    case Tool::Probe: return tr("Probe: hover over a body to read its velocity and forces (Esc to finish)");
    }
    return {};
}

// --- picking ---------------------------------------------------------------------------------------

ItemId Viewport::pick(QPointF screen, bool bodiesOnly) const
{
    const Vec2 w = toWorld(screen);
    const double tol = 4.0 / ppm_;
    const auto& items = doc_->scene().items;
    const bool live = sim_->live();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const ItemDef& d = *it;
        const ItemKind k = d.type->kind();
        if (k == ItemKind::Group || k == ItemKind::World) continue;
        if (bodiesOnly && k != ItemKind::Body) continue;
        if (!doc_->effectiveVisible(d) || doc_->effectiveLocked(d)) continue;
        if (live && k == ItemKind::Body && !bodySnap(d.id)) continue;
        if (d.type->hitTest(d, *this, w, tol)) return d.id;
    }
    return 0;
}

QList<ItemId> Viewport::transformTargets() const
{
    QList<ItemId> out;
    for (ItemId id : doc_->withDescendants(doc_->selection())) {
        const ItemDef* d = doc_->item(id);
        if (d && !doc_->effectiveLocked(*d) && d->type->kind() != ItemKind::Group) out << id;
    }
    return out;
}

bool Viewport::selectionPivot(Vec2& pivot, Pose& frame) const
{
    const QList<ItemId> targets = transformTargets();
    if (targets.isEmpty()) return false;
    if (targets.size() == 1) {
        const ItemDef* d = doc_->item(targets[0]);
        Pose p;
        if (d->type->hasPose() && pose(d->id, p)) {
            pivot = p.p;
            frame = p;
            return true;
        }
    }
    Aabb box;
    for (ItemId id : targets) {
        const ItemDef* d = doc_->item(id);
        box.add(d->type->bounds(*d, *this));
    }
    if (!box.valid()) return false;
    pivot = box.center();
    frame = Pose{pivot, 1, 0};
    return true;
}

Viewport::Drag Viewport::hitGizmo(QPointF screen) const
{
    if (tool_ != Tool::Select) return Drag::None;
    Vec2 pivot;
    Pose frame;
    if (!selectionPivot(pivot, frame)) return Drag::None;
    const QPointF c = toScreen(pivot);
    const QPointF d = screen - c;
    const double r = std::hypot(d.x(), d.y());
    // Axis directions on screen (y flipped).
    const Vec2 ax = gizmo_ == Gizmo::Move ? Vec2{1, 0} : frame.applyVector({1, 0});
    const Vec2 ay = gizmo_ == Gizmo::Move ? Vec2{0, 1} : frame.applyVector({0, 1});
    const QPointF hx = c + QPointF(ax.x, -ax.y) * kGizmoPx;
    const QPointF hy = c + QPointF(ay.x, -ay.y) * kGizmoPx;
    auto near = [&](QPointF p, double tol) { return std::hypot(screen.x() - p.x(), screen.y() - p.y()) <= tol; };
    auto nearSeg = [&](QPointF a, QPointF b) {
        return geom::distanceToSegment({screen.x(), screen.y()}, {a.x(), a.y()}, {b.x(), b.y()}) <= kHandlePx;
    };
    switch (gizmo_) {
    case Gizmo::Move:
        if (near(c, kHandlePx + 2)) return Drag::GizmoFree;
        if (nearSeg(c, hx)) return Drag::GizmoX;
        if (nearSeg(c, hy)) return Drag::GizmoY;
        break;
    case Gizmo::Rotate:
        if (std::abs(r - kGizmoPx) <= kHandlePx) return Drag::GizmoRotate;
        break;
    case Gizmo::Scale:
        if (near(c, kHandlePx + 2)) return Drag::GizmoScaleU;
        if (near(hx, kHandlePx + 2)) return Drag::GizmoScaleX;
        if (near(hy, kHandlePx + 2)) return Drag::GizmoScaleY;
        break;
    }
    return Drag::None;
}

void Viewport::applyTransformDrag(QPointF screen, Qt::KeyboardModifiers mods)
{
    const Vec2 w = toWorld(screen);
    const bool snap = snap_ != bool(mods & Qt::ControlModifier); // Ctrl toggles snapping while dragging
    std::vector<ItemDef> after = dragStart_;
    QString text;
    switch (drag_) {
    case Drag::MoveItems:
    case Drag::GizmoFree:
    case Drag::GizmoX:
    case Drag::GizmoY: {
        Vec2 delta = w - pressWorld_;
        if (drag_ == Drag::GizmoX) delta.y = 0;
        if (drag_ == Drag::GizmoY) delta.x = 0;
        if (snap) {
            const double s = gridStep();
            const Vec2 target = pivot_ + delta;
            Vec2 snapped{std::round(target.x / s) * s, std::round(target.y / s) * s};
            if (drag_ == Drag::GizmoX) snapped.y = target.y;
            if (drag_ == Drag::GizmoY) snapped.x = target.x;
            delta = snapped - pivot_;
        }
        for (ItemDef& d : after) d.type->translate(d, delta);
        text = tr("Move");
        break;
    }
    case Drag::GizmoRotate: {
        const Vec2 a0 = pressWorld_ - pivot_;
        const Vec2 a1 = w - pivot_;
        double da = std::atan2(a1.y, a1.x) - std::atan2(a0.y, a0.x);
        if (snap) {
            const double step = 15.0 / kDegPerRad;
            da = std::round(da / step) * step;
        }
        for (ItemDef& d : after) d.type->rotateAbout(d, da, pivot_);
        text = tr("Rotate");
        break;
    }
    case Drag::GizmoScaleX:
    case Drag::GizmoScaleY:
    case Drag::GizmoScaleU: {
        const Vec2 ax = gizmoFrame_.applyVector({1, 0});
        const Vec2 ay = gizmoFrame_.applyVector({0, 1});
        const Vec2 d0 = pressWorld_ - pivot_;
        const Vec2 d1 = w - pivot_;
        double sx = 1, sy = 1;
        if (drag_ == Drag::GizmoScaleX) sx = dot(d1, ax) / (std::abs(dot(d0, ax)) > 1e-9 ? dot(d0, ax) : 1e-9);
        if (drag_ == Drag::GizmoScaleY) sy = dot(d1, ay) / (std::abs(dot(d0, ay)) > 1e-9 ? dot(d0, ay) : 1e-9);
        if (drag_ == Drag::GizmoScaleU) {
            // Centre handle: drag right/up to grow.
            const double s = std::exp((screen.x() - pressScreen_.x() - (screen.y() - pressScreen_.y())) / 150.0);
            sx = sy = s;
        }
        sx = std::clamp(sx, 0.02, 50.0);
        sy = std::clamp(sy, 0.02, 50.0);
        if (snap) {
            sx = std::round(sx * 20.0) / 20.0;
            sy = std::round(sy * 20.0) / 20.0;
            sx = std::max(sx, 0.05);
            sy = std::max(sy, 0.05);
        }
        const bool multi = after.size() > 1;
        for (ItemDef& d : after) {
            if (multi) {
                const double s = drag_ == Drag::GizmoScaleY ? sy : sx;
                d.type->scale(d, s, s);
                if (d.type->hasPose()) {
                    Pose p = d.type->pose(d);
                    p.p = pivot_ + (p.p - pivot_) * s;
                    d.type->setPose(d, p);
                }
            } else {
                d.type->scale(d, sx, sy);
            }
        }
        text = tr("Scale");
        break;
    }
    default: return;
    }
    doc_->editItems(after, text, true);
}

// --- mouse & keyboard ---------------------------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    pressScreen_ = lastScreen_ = e->position();
    pressWorld_ = toWorld(e->position());
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && e->modifiers() & Qt::AltModifier)) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        drag_ = Drag::Pending; // becomes a pan if the mouse moves, else a context menu
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    switch (tool_) {
    case Tool::Select: {
        const Drag g = hitGizmo(e->position());
        if (g != Drag::None) {
            drag_ = g;
            selectionPivot(pivot_, gizmoFrame_);
            dragStart_.clear();
            for (ItemId id : transformTargets()) dragStart_.push_back(*doc_->item(id));
            return;
        }
        const ItemId hit = pick(e->position());
        const bool additive = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
        if (hit) {
            if (additive) {
                QList<ItemId> sel = doc_->selection();
                if (sel.contains(hit))
                    sel.removeAll(hit);
                else
                    sel << hit;
                doc_->setSelection(sel);
            } else if (!doc_->isSelected(hit)) {
                doc_->setSelection({hit});
            }
            drag_ = Drag::Pending;
            selectionPivot(pivot_, gizmoFrame_);
            dragStart_.clear();
            for (ItemId id : transformTargets()) dragStart_.push_back(*doc_->item(id));
        } else {
            preBoxSelection_ = additive ? doc_->selection() : QList<ItemId>{};
            if (!additive) doc_->setSelection({});
            drag_ = Drag::BoxSelect;
            boxA_ = boxB_ = pressWorld_;
        }
        return;
    }
    case Tool::Connect: {
        const ItemId body = pick(e->position(), true);
        Vec2 w = pressWorld_;
        if (!body) w = snapPoint(w);
        if (!connectHasA_) {
            connectHasA_ = true;
            connectBodyA_ = body;
            connectA_ = w;
            emit toolChanged();
            update();
        } else {
            finishConnect(w, body);
        }
        return;
    }
    case Tool::Draw:
        if (polygonMode_) {
            stroke_.push_back(snapPoint(pressWorld_));
        } else {
            stroke_.clear();
            stroke_.push_back(pressWorld_);
            drag_ = Drag::Stroke;
        }
        update();
        return;
    case Tool::Ruler:
        measure_.ruler = true;
        measure_.a = measure_.b = snapPoint(pressWorld_);
        drag_ = Drag::Ruler;
        update();
        return;
    case Tool::Protractor: {
        const Vec2 p = snapPoint(pressWorld_);
        if (measure_.protractorPoints == 0 || measure_.protractorPoints >= 3) {
            measure_.protractor = true;
            measure_.a = p;
            measure_.b = measure_.c = p;
            measure_.protractorPoints = 1;
        } else if (measure_.protractorPoints == 1) {
            measure_.b = measure_.c = p;
            measure_.protractorPoints = 2;
        } else {
            measure_.c = p;
            measure_.protractorPoints = 3;
        }
        update();
        return;
    }
    case Tool::Probe: return;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    cursorWorld_ = toWorld(pos);
    emit cursorMoved(cursorWorld_);
    const QPointF delta = pos - lastScreen_;
    lastScreen_ = pos;
    const double moved = std::hypot(pos.x() - pressScreen_.x(), pos.y() - pressScreen_.y());

    switch (drag_) {
    case Drag::Pan:
        setCamera(center_ + Vec2{-delta.x() / ppm_, delta.y() / ppm_}, ppm_);
        return;
    case Drag::Pending:
        if (moved < 3.0) return;
        if (e->buttons() & Qt::RightButton) {
            drag_ = Drag::Pan;
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (dragStart_.empty()) {
            drag_ = Drag::None;
            return;
        }
        drag_ = Drag::MoveItems;
        [[fallthrough]];
    case Drag::MoveItems:
    case Drag::GizmoX:
    case Drag::GizmoY:
    case Drag::GizmoFree:
    case Drag::GizmoRotate:
    case Drag::GizmoScaleX:
    case Drag::GizmoScaleY:
    case Drag::GizmoScaleU:
        applyTransformDrag(pos, e->modifiers());
        return;
    case Drag::BoxSelect: {
        boxB_ = cursorWorld_;
        Aabb box;
        box.add(boxA_);
        box.add(boxB_);
        QList<ItemId> sel = preBoxSelection_;
        for (const ItemDef& d : doc_->scene().items) {
            const ItemKind k = d.type->kind();
            if (k == ItemKind::Group || !doc_->effectiveVisible(d) || doc_->effectiveLocked(d)) continue;
            const Aabb b = d.type->bounds(d, *this);
            if (b.valid() && box.contains(b.lo) && box.contains(b.hi) && !sel.contains(d.id)) sel << d.id;
        }
        doc_->setSelection(sel);
        update();
        return;
    }
    case Drag::Stroke:
        if (stroke_.empty() || distance(stroke_.back(), cursorWorld_) * ppm_ >= 2.0) stroke_.push_back(cursorWorld_);
        update();
        return;
    case Drag::Ruler:
        measure_.b = snapPoint(cursorWorld_);
        update();
        return;
    default: break;
    }

    if (tool_ == Tool::Select) {
        const ItemId h = pick(pos);
        const Drag g = hitGizmo(pos);
        if (h != hover_) {
            hover_ = h;
            update();
        }
        setCursor(g != Drag::None ? Qt::SizeAllCursor : (h ? Qt::PointingHandCursor : Qt::ArrowCursor));
    } else if (tool_ == Tool::Protractor && measure_.protractorPoints == 2) {
        measure_.c = snapPoint(cursorWorld_);
        update();
    } else if (tool_ == Tool::Connect || tool_ == Tool::Draw || tool_ == Tool::Probe) {
        if (tool_ == Tool::Probe) {
            const ItemId h = pick(pos, true);
            if (h != hover_) hover_ = h;
        }
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e)
{
    const Drag d = drag_;
    drag_ = Drag::None;
    switch (d) {
    case Drag::Pan: setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor); break;
    case Drag::Pending:
        if (e->button() == Qt::RightButton) emit contextMenuRequested(e->globalPosition().toPoint());
        break;
    case Drag::MoveItems:
    case Drag::GizmoX:
    case Drag::GizmoY:
    case Drag::GizmoFree:
    case Drag::GizmoRotate:
    case Drag::GizmoScaleX:
    case Drag::GizmoScaleY:
    case Drag::GizmoScaleU: doc_->endContinuousEdit(); break;
    case Drag::BoxSelect: update(); break;
    case Drag::Stroke: {
        // A click without dragging switches to click-to-place polygon mode.
        const double moved = std::hypot(e->position().x() - pressScreen_.x(), e->position().y() - pressScreen_.y());
        if (moved < 4.0) {
            polygonMode_ = true;
            stroke_ = {snapPoint(pressWorld_)};
            emit toolChanged();
        } else {
            finishStroke();
        }
        update();
        break;
    }
    default: break;
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (tool_ == Tool::Draw && polygonMode_) {
        finishStroke();
        return;
    }
    if (tool_ == Tool::Select && e->button() == Qt::LeftButton && !pick(e->position())) zoomToFit();
}

void Viewport::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0.0) zoomBy(std::pow(1.15, steps), e->position());
    e->accept();
}

void Viewport::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (tool_ == Tool::Connect && connectHasA_) {
            connectHasA_ = false;
            update();
        } else if (tool_ != Tool::Select) {
            cancelTool();
        } else {
            doc_->setSelection({});
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (tool_ == Tool::Draw && polygonMode_) finishStroke();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const double s = (e->modifiers() & Qt::ShiftModifier) ? gridStep() * 10 : gridStep();
        Vec2 d;
        if (e->key() == Qt::Key_Left) d.x = -s;
        if (e->key() == Qt::Key_Right) d.x = s;
        if (e->key() == Qt::Key_Up) d.y = s;
        if (e->key() == Qt::Key_Down) d.y = -s;
        std::vector<ItemDef> after;
        for (ItemId id : transformTargets()) {
            ItemDef def = *doc_->item(id);
            def.type->translate(def, d);
            after.push_back(def);
        }
        doc_->editItems(after, tr("Nudge"), e->isAutoRepeat());
        if (!e->isAutoRepeat()) doc_->endContinuousEdit();
        return;
    }
    default: QRhiWidget::keyPressEvent(e);
    }
}

void Viewport::leaveEvent(QEvent* e)
{
    if (hover_) {
        hover_ = 0;
        update();
    }
    QRhiWidget::leaveEvent(e);
}

void Viewport::finishConnect(Vec2 world, ItemId body)
{
    if (!connectType_) return;
    const auto& sch = connectType_->schema();
    ItemDef d = connectType_->makeDefault(doc_->allocateId(), world);
    auto setRef = [&](const char* key, ItemId id) {
        const int i = sch.indexOf(QLatin1String(key));
        if (i >= 0) d.set(i, int64_t(id));
    };
    auto setAnchor = [&](const char* key, ItemId id, Vec2 w) {
        const int i = sch.indexOf(QLatin1String(key));
        if (i < 0) return;
        Pose p;
        d.set(i, (id && pose(id, p)) ? p.inverse(w) : w);
    };
    if (body == connectBodyA_ && body != 0) {
        // Same body twice: attach the second end to the world instead.
        body = 0;
    }
    setRef("bodyA", connectBodyA_);
    setAnchor("anchorA", connectBodyA_, connectA_);
    setRef("bodyB", body);
    setAnchor("anchorB", body, world);
    connectType_->initConnection(d, *this);
    connectType_->propertyChanged(d, -1);
    const ItemType* t = connectType_;
    doc_->addItems({d}, tr("Add %1").arg(t->meta().name));
    // Stay in connect mode for quick chains (Esc leaves).
    connectHasA_ = false;
    emit toolChanged();
    update();
}

void Viewport::finishStroke()
{
    const ItemType* type = drawType_;
    PointList pts = stroke_;
    const bool wasPolygon = polygonMode_;
    stroke_.clear();
    polygonMode_ = false;
    if (!type) return;
    if (!wasPolygon) pts = geom::simplify(pts, 2.5 / ppm_);
    pts = geom::cleanPolygon(pts, std::max(0.01, 3.0 / ppm_));
    if (pts.size() < 3 || std::abs(geom::signedArea(pts.data(), int(pts.size()))) < 1e-4) {
        emit toolChanged();
        update();
        return;
    }
    if (!geom::isSimplePolygon(pts)) pts = geom::convexHull(pts); // self-intersecting stroke
    geom::makeCcw(pts);
    const Vec2 c = geom::polygonCentroid(pts.data(), int(pts.size()));
    for (Vec2& p : pts) p -= c;
    ItemDef d = type->makeDefault(doc_->allocateId(), c);
    const int oi = type->schema().indexOf(QStringLiteral("outline"));
    if (oi >= 0) d.set(oi, pts);
    type->propertyChanged(d, oi);
    doc_->addItems({d}, tr("Draw %1").arg(type->meta().name));
    emit toolChanged();
    update();
}

// --- drag & drop from the toolbox ------------------------------------------------------------------

void Viewport::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasFormat(QLatin1String(kItemMime))) return;
    dropType_ = ItemRegistry::instance().find(QString::fromUtf8(e->mimeData()->data(QLatin1String(kItemMime))));
    if (!dropType_) return;
    dropWorld_ = snapPoint(toWorld(e->position()));
    ghost_ = dropType_->makeDefault(0, dropWorld_);
    e->acceptProposedAction();
    update();
}

void Viewport::dragMoveEvent(QDragMoveEvent* e)
{
    if (!dropType_) return;
    dropWorld_ = snapPoint(toWorld(e->position()));
    if (dropType_->hasPose()) {
        Pose p = dropType_->pose(ghost_);
        p.p = dropWorld_;
        dropType_->setPose(ghost_, p);
    }
    e->acceptProposedAction();
    update();
}

void Viewport::dragLeaveEvent(QDragLeaveEvent*)
{
    dropType_ = nullptr;
    update();
}

void Viewport::dropEvent(QDropEvent* e)
{
    const ItemType* t = dropType_;
    dropType_ = nullptr;
    if (!t) return;
    e->acceptProposedAction();
    setFocus();
    placeItem(t, toWorld(e->position()));
    update();
}

void Viewport::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange ||
        e->type() == QEvent::ApplicationPaletteChange) {
        updateTheme();
        update();
    }
    QRhiWidget::changeEvent(e);
}

void Viewport::resizeEvent(QResizeEvent* e)
{
    QRhiWidget::resizeEvent(e);
    overlay_->setGeometry(rect());
}

void Viewport::updateTheme()
{
    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    theme_.background = toColor(base, 255);
    theme_.dark = base.lightness() < 128;
    theme_.text = toColor(text, 255);
    theme_.grid = toColor(text, theme_.dark ? 22 : 18);
    theme_.gridMajor = toColor(text, theme_.dark ? 45 : 40);
    theme_.axis = toColor(text, 100);
    theme_.outline = toColor(text, 170);
    theme_.dim = toColor(text, 110);
    theme_.highlight = toColor(pal.color(QPalette::Highlight), 255);
}

// --- frame building ------------------------------------------------------------------------------------

void Viewport::drawGrid()
{
    const Vec2 lo = toWorld(QPointF(0, height()));
    const Vec2 hi = toWorld(QPointF(width(), 0));
    const double step = gridStep();
    const double major = step * 5.0;
    const long x0 = long(std::floor(lo.x / step)), x1 = long(std::ceil(hi.x / step));
    const long y0 = long(std::floor(lo.y / step)), y1 = long(std::ceil(hi.y / step));
    if (x1 - x0 > 1000 || y1 - y0 > 1000) return;
    for (long i = x0; i <= x1; ++i) {
        const double x = double(i) * step;
        const bool isMajor = std::abs(std::remainder(x, major)) < step * 0.01;
        batch_.line(Layer::Back, {x, lo.y}, {x, hi.y}, 1.0f, i == 0 ? theme_.axis : (isMajor ? theme_.gridMajor : theme_.grid));
    }
    for (long j = y0; j <= y1; ++j) {
        const double y = double(j) * step;
        const bool isMajor = std::abs(std::remainder(y, major)) < step * 0.01;
        batch_.line(Layer::Back, {lo.x, y}, {hi.x, y}, 1.0f, j == 0 ? theme_.axis : (isMajor ? theme_.gridMajor : theme_.grid));
    }
}

void Viewport::drawItems()
{
    const bool live = sim_->live();
    const double time = live ? sim_->snapshot().time : 0.0;
    for (const ItemDef& d : doc_->scene().items) {
        const ItemKind k = d.type->kind();
        if (k == ItemKind::Group || k == ItemKind::World) continue;
        if (!d.visible || !doc_->effectiveVisible(d)) continue;
        const BodySnap* bs = nullptr;
        if (live && k == ItemKind::Body) {
            bs = bodySnap(d.id);
            if (!bs) continue; // not part of the simulation at this frame
        }
        DrawContext ctx{batch_, theme_, *this};
        ctx.pxPerM = ppm_;
        ctx.selected = selected_.count(d.id) > 0 || (hover_ == d.id && tool_ == Tool::Select);
        ctx.simActive = live;
        ctx.time = time;
        ctx.body = bs;
        ctx.snap = live ? itemSnap(d.id) : nullptr;
        if (bs)
            ctx.pose = bs->pose();
        else if (d.type->hasPose())
            ctx.pose = d.type->pose(d);
        d.type->draw(d, ctx);
    }
    if (dropType_) {
        DrawContext ctx{batch_, theme_, *this};
        ctx.pxPerM = ppm_;
        ctx.selected = true;
        ctx.pose = dropType_->hasPose() ? dropType_->pose(ghost_) : Pose{dropWorld_, 1, 0};
        dropType_->draw(ghost_, ctx);
    }
}

void Viewport::drawSelectionAndGizmo()
{
    const Color hl = theme_.highlight;
    if (drag_ == Drag::BoxSelect) {
        Aabb b;
        b.add(boxA_);
        b.add(boxB_);
        batch_.rectFill(Layer::Overlay, b.lo, b.hi, hl.withAlpha(40));
        const Vec2 pts[4] = {b.lo, {b.hi.x, b.lo.y}, b.hi, {b.lo.x, b.hi.y}};
        batch_.polyline(Layer::Overlay, pts, 4, true, 1.0f, hl);
    }
    // Dashed bounds make the selection obvious whatever the item colours are.
    if (selected_.size() <= 200) {
        const double pad = 3.0 / ppm_;
        for (ItemId id : doc_->selection()) {
            const ItemDef* d = doc_->item(id);
            if (!d || d->type->kind() == ItemKind::Group) continue;
            Aabb b = d->type->bounds(*d, *this);
            if (!b.valid()) continue;
            b.inflate(pad);
            const Vec2 c[4] = {b.lo, {b.hi.x, b.lo.y}, b.hi, {b.lo.x, b.hi.y}};
            for (int i = 0; i < 4; ++i) batch_.dashed(Layer::Overlay, c[i], c[(i + 1) % 4], 1.0f, hl.withAlpha(200), 4.0);
        }
    }
    if (tool_ != Tool::Select || drag_ == Drag::BoxSelect) return;
    Vec2 pivot;
    Pose frame;
    if (!selectionPivot(pivot, frame)) return;
    const double L = kGizmoPx / ppm_;
    const double h = kHandlePx / ppm_;
    const Color red = Color::fromRgb(0xe74c3c), green = Color::fromRgb(0x27ae60);
    switch (gizmo_) {
    case Gizmo::Move:
        batch_.arrow(Layer::Overlay, pivot, pivot + Vec2{L, 0}, 2.5f, red, 12);
        batch_.arrow(Layer::Overlay, pivot, pivot + Vec2{0, L}, 2.5f, green, 12);
        batch_.rectFill(Layer::Overlay, pivot - Vec2{h, h}, pivot + Vec2{h, h}, hl.withAlpha(160));
        break;
    case Gizmo::Rotate:
        batch_.circleOutline(Layer::Overlay, pivot, L, hl, 2.5f);
        batch_.disc(Layer::Overlay, pivot + frame.applyVector({L, 0}), h, hl, hl, 1.0f);
        break;
    case Gizmo::Scale: {
        const Vec2 ax = frame.applyVector({L, 0});
        const Vec2 ay = frame.applyVector({0, L});
        batch_.line(Layer::Overlay, pivot, pivot + ax, 2.5f, red);
        batch_.line(Layer::Overlay, pivot, pivot + ay, 2.5f, green);
        batch_.rectFill(Layer::Overlay, pivot + ax - Vec2{h, h}, pivot + ax + Vec2{h, h}, red);
        batch_.rectFill(Layer::Overlay, pivot + ay - Vec2{h, h}, pivot + ay + Vec2{h, h}, green);
        batch_.rectFill(Layer::Overlay, pivot - Vec2{h, h}, pivot + Vec2{h, h}, hl.withAlpha(160));
        break;
    }
    }
}

void Viewport::drawToolPreview()
{
    const Color hl = theme_.highlight;
    if (tool_ == Tool::Connect && connectHasA_) {
        Vec2 a = connectA_;
        batch_.dashed(Layer::Overlay, a, cursorWorld_, 1.5f, hl);
        batch_.disc(Layer::Overlay, a, 4.0 / ppm_, hl, hl);
    }
    if (tool_ == Tool::Connect) batch_.circleOutline(Layer::Overlay, cursorWorld_, 5.0 / ppm_, hl, 1.5f);
    if (tool_ == Tool::Draw && !stroke_.empty()) {
        batch_.polyline(Layer::Overlay, stroke_.data(), int(stroke_.size()), false, 2.0f, hl);
        if (polygonMode_) {
            for (const Vec2& p : stroke_) batch_.disc(Layer::Overlay, p, 3.0 / ppm_, hl, hl);
            batch_.dashed(Layer::Overlay, stroke_.back(), snapPoint(cursorWorld_), 1.0f, hl);
            batch_.dashed(Layer::Overlay, snapPoint(cursorWorld_), stroke_.front(), 1.0f, hl.withAlpha(120));
        }
    }
    const Color tc = theme_.text;
    if (measure_.ruler) {
        const Vec2 a = measure_.a, b = measure_.b;
        batch_.line(Layer::Overlay, a, b, 2.0f, tc);
        const Vec2 n = perp(normalized(b - a)) * (6.0 / ppm_);
        batch_.line(Layer::Overlay, a - n, a + n, 2.0f, tc);
        batch_.line(Layer::Overlay, b - n, b + n, 2.0f, tc);
        batch_.dashed(Layer::Overlay, a, {b.x, a.y}, 1.0f, theme_.dim);
        batch_.dashed(Layer::Overlay, {b.x, a.y}, b, 1.0f, theme_.dim);
        static const QString dLabel = QStringLiteral("d = "), mUnit = QStringLiteral(" m");
        static const QString dxLabel = QStringLiteral("Δx = "), dyLabel = QStringLiteral("Δy = ");
        batch_.valueLabel((a + b) * 0.5, dLabel, distance(a, b), mUnit, 3, tc, 8, -14);
        batch_.valueLabel({(a.x + b.x) * 0.5, a.y}, dxLabel, b.x - a.x, mUnit, 3, theme_.dim, 0, 14);
        batch_.valueLabel({b.x, (a.y + b.y) * 0.5}, dyLabel, b.y - a.y, mUnit, 3, theme_.dim, 8, 0);
    }
    if (measure_.protractor && measure_.protractorPoints >= 1) {
        const Vec2 v = measure_.a;
        batch_.disc(Layer::Overlay, v, 3.0 / ppm_, tc, tc);
        if (measure_.protractorPoints >= 2) {
            batch_.line(Layer::Overlay, v, measure_.b, 2.0f, tc);
            batch_.line(Layer::Overlay, v, measure_.c, 2.0f, tc);
            const double a0 = std::atan2(measure_.b.y - v.y, measure_.b.x - v.x);
            const double a1 = std::atan2(measure_.c.y - v.y, measure_.c.x - v.x);
            double da = wrapAngle(a1 - a0);
            const double r = 30.0 / ppm_;
            batch_.arc(Layer::Overlay, v, r, a0, a0 + da, 1.5f, tc, 24);
            static const QString thetaLabel = QStringLiteral("θ = "), degUnit = QStringLiteral("°");
            const double mid = a0 + da * 0.5;
            batch_.valueLabel(v + Vec2{std::cos(mid), std::sin(mid)} * (r * 1.6), thetaLabel, std::abs(da) * kDegPerRad,
                              degUnit, 1, tc);
        }
    }
}

void Viewport::buildFrame()
{
    batch_.clear();
    batch_.pxPerM = ppm_;
    if (grid_) drawGrid();
    drawItems();
    drawSelectionAndGizmo();
    drawToolPreview();
}

// --- RHI -----------------------------------------------------------------------------------------------------

QRhiGraphicsPipeline* Viewport::makePipeline(const char* vs, const char* fs,
                                             const QList<QRhiVertexInputBinding>& bindings,
                                             const QList<QRhiVertexInputAttribute>& attrs, bool additive,
                                             QRhiGraphicsPipeline::Topology topo)
{
    QRhiGraphicsPipeline* p = rhi_->newGraphicsPipeline();
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstColor = additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.dstAlpha = additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
    p->setTargetBlends({blend});
    p->setTopology(topo);
    p->setSampleCount(sampleCount());
    p->setShaderStages({{QRhiShaderStage::Vertex, loadShader(vs)}, {QRhiShaderStage::Fragment, loadShader(fs)}});
    QRhiVertexInputLayout layout;
    layout.setBindings(bindings.begin(), bindings.end());
    layout.setAttributes(attrs.begin(), attrs.end());
    p->setVertexInputLayout(layout);
    p->setShaderResourceBindings(srb_.get());
    p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!p->create()) qWarning("pipeline creation failed");
    return p;
}

void Viewport::initialize(QRhiCommandBuffer* cb)
{
    if (rhi_ != rhi() || sampleCountUsed_ != sampleCount()) {
        releaseResources();
        rhi_ = rhi();
        sampleCountUsed_ = sampleCount();
    }
    if (segPipe_) return;

    ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Ubuf)));
    ubuf_->create();
    static const float corners[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    quad_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(corners)));
    quad_->create();
    QRhiResourceUpdateBatch* rub = rhi_->nextResourceUpdateBatch();
    rub->uploadStaticBuffer(quad_.get(), corners);
    cb->resourceUpdate(rub);

    srb_.reset(rhi_->newShaderResourceBindings());
    srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_.get())});
    srb_->create();

    const QRhiVertexInputBinding quadB(2 * sizeof(float));
    const QRhiVertexInputBinding segB(sizeof(SegInst), QRhiVertexInputBinding::PerInstance);
    const QList<QRhiVertexInputAttribute> segA = {
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {1, 1, QRhiVertexInputAttribute::Float4, offsetof(SegInst, x0)},
        {1, 2, QRhiVertexInputAttribute::Float, offsetof(SegInst, width)},
        {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(SegInst, color)},
    };
    segPipe_.reset(makePipeline("segment.vert", "segment.frag", {quadB, segB}, segA, false,
                                QRhiGraphicsPipeline::Triangles));
    rayPipe_.reset(makePipeline("segment.vert", "segment.frag", {quadB, segB}, segA, true,
                                QRhiGraphicsPipeline::Triangles));
    const QRhiVertexInputBinding discB(sizeof(DiscInst), QRhiVertexInputBinding::PerInstance);
    discPipe_.reset(makePipeline("disc.vert", "disc.frag", {quadB, discB},
                                 {
                                     {0, 0, QRhiVertexInputAttribute::Float2, 0},
                                     {1, 1, QRhiVertexInputAttribute::Float4, offsetof(DiscInst, cx)},
                                     {1, 2, QRhiVertexInputAttribute::Float3, offsetof(DiscInst, radius)},
                                     {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(DiscInst, fill)},
                                     {1, 4, QRhiVertexInputAttribute::UNormByte4, offsetof(DiscInst, outline)},
                                 },
                                 false, QRhiGraphicsPipeline::Triangles));
    triPipe_.reset(makePipeline("tri.vert", "tri.frag", {QRhiVertexInputBinding(sizeof(TriVert))},
                                {
                                    {0, 0, QRhiVertexInputAttribute::Float2, offsetof(TriVert, x)},
                                    {0, 1, QRhiVertexInputAttribute::UNormByte4, offsetof(TriVert, color)},
                                },
                                false, QRhiGraphicsPipeline::Triangles));
}

void Viewport::releaseResources()
{
    segPipe_.reset();
    rayPipe_.reset();
    discPipe_.reset();
    triPipe_.reset();
    srb_.reset();
    ubuf_.reset();
    quad_.reset();
    segBuf_.reset();
    discBuf_.reset();
    triBuf_.reset();
}

bool Viewport::uploadBuffer(QRhiResourceUpdateBatch* rub, std::unique_ptr<QRhiBuffer>& buf, const void* data,
                            quint32 size, QRhiBuffer::UsageFlags usage)
{
    if (size == 0) return false;
    if (!buf || buf->size() < size) {
        // Geometric growth: reallocation only happens when the scene grows.
        buf.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, usage, std::max<quint32>(size + size / 2, 16384)));
        if (!buf->create()) return false;
    }
    rub->updateDynamicBuffer(buf.get(), 0, size, data);
    return true;
}

void Viewport::render(QRhiCommandBuffer* cb)
{
    alloc::Scope allocs;
    if (!segPipe_) return;
    static QElapsedTimer clock;
    if (!clock.isValid()) clock.start();
    const qint64 t0 = clock.nsecsElapsed();
    buildFrame();

    // Concatenate layers; remember where each layer starts.
    int segOff[kLayerCount + 1], discOff[kLayerCount + 1], triOff[kLayerCount + 1];
    segScratch_.clear();
    discScratch_.clear();
    triScratch_.clear();
    for (int l = 0; l < kLayerCount; ++l) {
        segOff[l] = int(segScratch_.size());
        segScratch_.insert(segScratch_.end(), batch_.segs[l].begin(), batch_.segs[l].end());
        discOff[l] = int(discScratch_.size());
        discScratch_.insert(discScratch_.end(), batch_.discs[l].begin(), batch_.discs[l].end());
        triOff[l] = int(triScratch_.size());
        triScratch_.insert(triScratch_.end(), batch_.tris[l].begin(), batch_.tris[l].end());
    }
    segOff[kLayerCount] = int(segScratch_.size());
    discOff[kLayerCount] = int(discScratch_.size());
    triOff[kLayerCount] = int(triScratch_.size());
    const int rayOff = int(segScratch_.size());
    segScratch_.insert(segScratch_.end(), batch_.rays.begin(), batch_.rays.end());

    QRhiResourceUpdateBatch* rub = rhi_->nextResourceUpdateBatch();
    const QSize px = renderTarget()->pixelSize();
    const float dpr = float(px.width()) / float(std::max(1, width()));
    QMatrix4x4 m;
    m.scale(float(2.0 * ppm_ / width()), float(2.0 * ppm_ / height()));
    m.translate(float(-center_.x), float(-center_.y));
    const QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix() * m;
    Ubuf u{};
    std::memcpy(u.mvp, mvp.constData(), sizeof(u.mvp));
    u.viewport[0] = float(px.width());
    u.viewport[1] = float(px.height());
    u.pxPerM = float(ppm_);
    u.dpr = dpr;
    rub->updateDynamicBuffer(ubuf_.get(), 0, sizeof(Ubuf), &u);
    const bool haveSeg = uploadBuffer(rub, segBuf_, segScratch_.data(), quint32(segScratch_.size() * sizeof(SegInst)),
                                      QRhiBuffer::VertexBuffer);
    const bool haveDisc = uploadBuffer(rub, discBuf_, discScratch_.data(),
                                       quint32(discScratch_.size() * sizeof(DiscInst)), QRhiBuffer::VertexBuffer);
    const bool haveTri = uploadBuffer(rub, triBuf_, triScratch_.data(), quint32(triScratch_.size() * sizeof(TriVert)),
                                      QRhiBuffer::VertexBuffer);

    const QColor clear(theme_.background.r, theme_.background.g, theme_.background.b);
    cb->beginPass(renderTarget(), clear, {1.0f, 0}, rub);
    cb->setViewport({0, 0, float(px.width()), float(px.height())});

    auto drawSegs = [&](QRhiGraphicsPipeline* pipe, int from, int count) {
        if (!haveSeg || count <= 0) return;
        cb->setGraphicsPipeline(pipe);
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput in[] = {{quad_.get(), 0},
                                                     {segBuf_.get(), quint32(from * int(sizeof(SegInst)))}};
        cb->setVertexInput(0, 2, in);
        cb->draw(6, quint32(count));
    };
    for (int l = 0; l < kLayerCount; ++l) {
        const int triCount = triOff[l + 1] - triOff[l];
        if (haveTri && triCount > 0) {
            cb->setGraphicsPipeline(triPipe_.get());
            cb->setShaderResources();
            const QRhiCommandBuffer::VertexInput in[] = {{triBuf_.get(), quint32(triOff[l] * int(sizeof(TriVert)))}};
            cb->setVertexInput(0, 1, in);
            cb->draw(quint32(triCount));
        }
        const int discCount = discOff[l + 1] - discOff[l];
        if (haveDisc && discCount > 0) {
            cb->setGraphicsPipeline(discPipe_.get());
            cb->setShaderResources();
            const QRhiCommandBuffer::VertexInput in[] = {{quad_.get(), 0},
                                                         {discBuf_.get(), quint32(discOff[l] * int(sizeof(DiscInst)))}};
            cb->setVertexInput(0, 2, in);
            cb->draw(6, quint32(discCount));
        }
        drawSegs(segPipe_.get(), segOff[l], segOff[l + 1] - segOff[l]);
        if (l == int(Layer::Front)) drawSegs(rayPipe_.get(), rayOff, int(batch_.rays.size()));
    }
    cb->endPass();

    lastAllocs_ = allocs.count();
    ++frames_;
    const qint64 t1 = clock.nsecsElapsed();
    const double ms = double(t1 - t0) / 1e6;
    renderMs_ = renderMs_ == 0 ? ms : 0.9 * renderMs_ + 0.1 * ms;
    ++fpsFrames_;
    if (t1 - fpsWindowStart_ >= 1000000000LL) {
        fps_ = fpsFrames_ * 1e9 / double(t1 - fpsWindowStart_);
        fpsWindowStart_ = t1;
        fpsFrames_ = 0;
    }
    overlay_->update();
}

} // namespace kn
