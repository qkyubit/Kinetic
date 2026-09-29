// Scene viewport: QRhiWidget renderer (instanced/batched draws) plus all
// editing interaction (selection, gizmos, drawing, connecting, measuring).
#pragma once

#include "Document.h"
#include "SceneOverlays.h"
#include "SimController.h"

#include "core/RenderBatch.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace kn {

class ViewportOverlay;
class Analysis;

class Viewport : public QRhiWidget, public SceneView {
    Q_OBJECT
public:
    enum class Tool { Select, Connect, Draw, Ruler, Protractor, Probe };
    enum class Gizmo { Move, Rotate, Scale };

    Viewport(Document* doc, SimController* sim, QWidget* parent = nullptr);
    ~Viewport() override;

    // SceneView
    const ItemDef* def(ItemId id) const override;
    bool pose(ItemId id, Pose& out) const override;

    // Camera: one pixels-per-metre scale; +y is up.
    Vec2 center() const { return center_; }
    double pixelsPerMetre() const { return ppm_; }
    void setCamera(Vec2 center, double ppm);
    Vec2 toWorld(QPointF p) const;
    QPointF toScreen(Vec2 w) const;
    void zoomToFit();
    void zoomBy(double factor, QPointF anchor);
    double gridStep() const;

    Tool tool() const { return tool_; }
    void setTool(Tool t);
    Gizmo gizmo() const { return gizmo_; }
    void setGizmo(Gizmo g);
    bool snapping() const { return snap_; }
    void setSnapping(bool on) { snap_ = on; }
    bool gridVisible() const { return grid_; }
    void setGridVisible(bool on);
    // Starts two-click connector creation / freehand drawing for an item type.
    void beginConnect(const ItemType* type);
    void beginDraw(const ItemType* type);
    // Places a new item of `type` at a world point (or the view centre).
    void placeItem(const ItemType* type, Vec2 at);

    const RenderBatch& batch() const { return batch_; }
    const ThemeColors& theme() const { return theme_; }
    uint64_t lastRenderAllocations() const { return lastAllocs_; }
    const SimController* simulation() const { return sim_; }
    Analysis* analysis() const { return analysis_.get(); }
    // Rendering statistics (shown by View > Performance Statistics).
    double fps() const { return fps_; }
    double renderMs() const { return renderMs_; }
    uint64_t frames() const { return frames_; }
    bool statsVisible() const { return showStats_; }
    void setStatsVisible(bool on)
    {
        showStats_ = on;
        update();
    }
    const BodySnap* bodySnap(ItemId id) const;
    const ItemSnap* itemSnap(ItemId id) const;
    // Topmost selectable item at a screen point (bodies only if bodiesOnly).
    ItemId pick(QPointF screen, bool bodiesOnly = false) const;

    // Ruler/protractor readouts for the overlay.
    struct Measure {
        bool ruler = false, protractor = false;
        Vec2 a, b, c; // ruler: a->b; protractor: vertex a, arms b and c
        int protractorPoints = 0;
    };
    const Measure& measure() const { return measure_; }
    QString statusHint() const;

signals:
    void cursorMoved(kn::Vec2 world);
    void viewChanged();
    void toolChanged();
    void contextMenuRequested(QPoint global);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    enum class Drag {
        None, Pending, Pan, BoxSelect, MoveItems, GizmoX, GizmoY, GizmoFree, GizmoRotate, GizmoScaleX,
        GizmoScaleY, GizmoScaleU, Stroke, Ruler, Protractor
    };

    void updateTheme();
    void buildFrame();
    void drawGrid();
    void drawItems();
    void drawSelectionAndGizmo();
    void drawToolPreview();
    bool uploadBuffer(QRhiResourceUpdateBatch* rub, std::unique_ptr<QRhiBuffer>& buf, const void* data, quint32 size,
                      QRhiBuffer::UsageFlags usage);
    QRhiGraphicsPipeline* makePipeline(const char* vs, const char* fs, const QList<QRhiVertexInputBinding>& bindings,
                                       const QList<QRhiVertexInputAttribute>& attrs, bool additive,
                                       QRhiGraphicsPipeline::Topology topo);

    Vec2 snapPoint(Vec2 w) const;
    QList<ItemId> transformTargets() const;
    Drag hitGizmo(QPointF screen) const;
    bool selectionPivot(Vec2& pivot, Pose& frame) const;
    void applyTransformDrag(QPointF screen, Qt::KeyboardModifiers mods);
    void finishStroke();
    void cancelTool();
    void finishConnect(Vec2 world, ItemId body);

    Document* doc_;
    SimController* sim_;
    std::unique_ptr<Analysis> analysis_;
    FieldOverlay fieldOverlay_;
    OpticsView opticsView_;
    ViewportOverlay* overlay_ = nullptr;

    Vec2 center_{0.0, 4.0};
    double ppm_ = 50.0;
    Tool tool_ = Tool::Select;
    Gizmo gizmo_ = Gizmo::Move;
    bool snap_ = true;
    bool grid_ = true;

    // Interaction state.
    Drag drag_ = Drag::None;
    QPointF pressScreen_;
    QPointF lastScreen_;
    Vec2 pressWorld_;
    Vec2 pivot_;
    Pose gizmoFrame_;
    std::vector<ItemDef> dragStart_;
    // While simulating: the same items at their live poses, and which of them
    // are live bodies (moved in the simulation, not in the document).
    std::vector<ItemDef> dragStartLive_;
    std::vector<char> dragLiveBody_;
    std::vector<std::pair<ItemId, Pose>> heldBodies_; // re-pinned every frame while running
    void captureDragStart();
    QList<ItemId> preBoxSelection_;
    Vec2 boxA_, boxB_;
    ItemId hover_ = 0;
    const ItemType* connectType_ = nullptr;
    bool connectHasA_ = false;
    ItemId connectBodyA_ = 0;
    Vec2 connectA_;
    const ItemType* drawType_ = nullptr;
    PointList stroke_;
    bool polygonMode_ = false;
    Vec2 cursorWorld_;
    const ItemType* dropType_ = nullptr;
    Vec2 dropWorld_;
    Measure measure_;

    std::unordered_set<ItemId> selected_;
    ItemDef ghost_;

    // Rendering.
    ThemeColors theme_;
    RenderBatch batch_;
    uint64_t lastAllocs_ = 0;
    uint64_t frames_ = 0;
    bool showStats_ = false;
    double fps_ = 0, renderMs_ = 0;
    qint64 fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
    QRhi* rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> ubuf_, quad_, segBuf_, discBuf_, triBuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> segPipe_, rayPipe_, discPipe_, triPipe_;
    std::vector<SegInst> segScratch_;
    std::vector<DiscInst> discScratch_;
    std::vector<TriVert> triScratch_;
    int sampleCountUsed_ = 0;
};

} // namespace kn
