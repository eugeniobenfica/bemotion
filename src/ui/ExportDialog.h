#pragma once

#include "export/Export.h"

#include <string>

namespace bemotion {

class EditorContext;

// "Export Video" modal: settings, then progress, then the result.
class ExportDialog {
public:
    void open(EditorContext& ctx);
    void draw(EditorContext& ctx);
    bool busy() const { return job_.running(); }

private:
    enum class Stage { Closed, Settings, Preparing, Running, Done };

    void drawSettings(EditorContext& ctx);
    void drawProgress();
    void drawResult();
    void startExport(EditorContext& ctx);
    void setFormat(exporting::Format format);

    Stage stage_ = Stage::Closed;
    bool openPopup_ = false;
    exporting::Settings settings_;
    int scaleIndex_ = 0;
    bool customRange_ = false;
    float rangeStart_ = 0.0f;
    float rangeEnd_ = 0.0f;
    std::string error_;
    exporting::ExportJob job_;
};

} // namespace bemotion
