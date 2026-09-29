// Analysis overlays drawn in the viewport: trails (ring buffers), force,
// velocity and acceleration vectors with magnitudes, the energy HUD and the
// probe readout.
#pragma once

#include "Document.h"
#include "SimController.h"

#include "core/RenderBatch.h"

#include <QObject>

#include <unordered_map>
#include <unordered_set>

class QPainter;

namespace kn {

struct AnalysisOptions {
    enum class ForceMode : int { Off, FlaggedAndSelected, All };
    enum class ScaleMode : int { RelativeToWeight, Absolute };
    ForceMode forces = ForceMode::FlaggedAndSelected;
    bool components = true;
    bool net = true;
    bool velocity = false;
    bool acceleration = false;
    bool labels = true;
    ScaleMode scaleMode = ScaleMode::RelativeToWeight;
    double weightPx = 60.0;      // arrow length of a force equal to the body's weight
    double pxPerNewton = 0.05;   // absolute mode
    double pxPerMps = 20.0;      // velocity arrows
    double pxPerMps2 = 6.0;      // acceleration arrows
    bool trails = true;          // global switch; bodies opt in with their "Show trail" property
    bool energy = false;         // energy HUD

    void load();
    void save() const;
};

class Analysis : public QObject {
    Q_OBJECT
public:
    Analysis(Document* doc, SimController* sim, QObject* parent = nullptr);

    AnalysisOptions& options() { return opts_; }
    void setOptions(const AnalysisOptions& o);
    void clearTrails();

    // Adds trails and vectors to the frame.
    void draw(RenderBatch& b, const ThemeColors& theme, double pxPerM, const std::unordered_set<ItemId>& selected);
    // Energy HUD and probe readout (text).
    void paintOverlay(QPainter& p, const ThemeColors& theme, QPointF cursor, ItemId probe, QSize size) const;

signals:
    void changed();

private:
    struct TrailPoint {
        double t;
        float x, y;
    };
    struct Trail {
        std::vector<TrailPoint> ring;
        size_t head = 0, count = 0;
        double length = 5.0; // seconds
    };

    void sample();
    void refreshTrailSet();
    void drawVectors(RenderBatch& b, const ThemeColors& theme, double pxPerM, const BodySnap& body, bool showForces);

    Document* doc_;
    SimController* sim_;
    AnalysisOptions opts_;
    std::unordered_map<ItemId, Trail> trails_;
    std::vector<ItemId> trailIds_; // bodies with "Show trail", refreshed on edits
    std::vector<ItemId> forceIds_; // bodies with "Show force vectors"
    uint64_t generation_ = ~0ull;
    double lastTime_ = -1.0;
};

} // namespace kn
