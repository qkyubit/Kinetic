// Controls for the analysis overlays: vectors, trails, energy.
#pragma once

#include <QWidget>

namespace kn {

class Analysis;
class Document;

class AnalysisPanel : public QWidget {
    Q_OBJECT
public:
    AnalysisPanel(Analysis* analysis, Document* doc, QWidget* parent = nullptr);

private:
    Analysis* analysis_;
    Document* doc_;
};

} // namespace kn
