#include "ui/ExportDialog.h"

#include "app/Platform.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace bemotion {

namespace {

constexpr const char* kPopup = "Export Video";

struct ScaleOption {
    const char* label;
    float scale;
};
constexpr ScaleOption kScales[] = {{"Full size", 1.0f}, {"Half size", 0.5f}, {"Quarter size", 0.25f}, {"Double (2x)", 2.0f}};
constexpr const char* kQualities[] = {"Low (smaller file)", "Medium", "High"};

std::string formatDuration(double seconds) {
    const int s = static_cast<int>(std::lround(std::max(0.0, seconds)));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

void label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(theme::kTextDim), "%s", text);
    ImGui::SameLine(130.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

} // namespace

void ExportDialog::open(EditorContext& ctx) {
    if (job_.running()) return;
    const Composition& comp = ctx.comp();
    if (settings_.output.empty()) {
        const auto folder = ctx.path.empty() ? platform::defaultProjectsDir() : ctx.path.parent_path();
        settings_.output = folder / platform::fromUtf8(ctx.project.name);
        setFormat(exporting::formatAvailable(settings_.format) ? settings_.format : exporting::Format::PngSequence);
    }
    if (!customRange_ || rangeEnd_ > comp.duration) {
        rangeStart_ = 0.0f;
        rangeEnd_ = static_cast<float>(comp.duration);
    }
    error_.clear();
    stage_ = Stage::Settings;
    openPopup_ = true;
}

void ExportDialog::setFormat(exporting::Format format) {
    settings_.format = format;
    settings_.output.replace_extension(exporting::formatExtension(format));
    if (!exporting::formatSupportsAlpha(format)) settings_.transparent = false;
}

void ExportDialog::draw(EditorContext& ctx) {
    if (stage_ == Stage::Closed) return;
    if (openPopup_) {
        ImGui::OpenPopup(kPopup);
        openPopup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 22));
    const bool visible = ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!visible) {
        if (!job_.running()) stage_ = Stage::Closed;
        return;
    }
    switch (stage_) {
    case Stage::Settings: drawSettings(ctx); break;
    case Stage::Preparing: // system fonts used by text layers load between frames
        ImGui::TextUnformatted("Preparing fonts...");
        startExport(ctx);
        break;
    case Stage::Running: drawProgress(); break;
    case Stage::Done: drawResult(); break;
    case Stage::Closed: break;
    }
    if (stage_ == Stage::Closed) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ExportDialog::drawSettings(EditorContext& ctx) {
    using namespace exporting;
    const Composition& comp = ctx.comp();

    label("Format");
    if (ImGui::BeginCombo("##format", formatLabel(settings_.format))) {
        for (int i = 0; i < static_cast<int>(Format::Count); ++i) {
            const auto f = static_cast<Format>(i);
            const bool available = formatAvailable(f);
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(formatLabel(f), f == settings_.format)) setFormat(f);
            ImGui::EndDisabled();
            if (!available) {
                ImGui::SameLine();
                ImGui::TextColored(theme::vec(theme::kTextFaint), "needs FFmpeg");
            }
        }
        ImGui::EndCombo();
    }

    label("Size");
    if (ImGui::BeginCombo("##scale", kScales[scaleIndex_].label)) {
        for (int i = 0; i < static_cast<int>(std::size(kScales)); ++i)
            if (ImGui::Selectable(kScales[i].label, i == scaleIndex_)) scaleIndex_ = i;
        ImGui::EndCombo();
    }
    settings_.scale = kScales[scaleIndex_].scale;
    int w = 0, h = 0;
    outputSize(comp, settings_, w, h);
    ImGui::SetCursorPosX(130.0f);
    ImGui::TextColored(theme::vec(theme::kTextFaint), "%d × %d px  ·  %.4g fps", w, h, comp.fps);

    if (settings_.format != Format::PngSequence) {
        label("Quality");
        int q = static_cast<int>(settings_.quality);
        if (ImGui::Combo("##quality", &q, kQualities, static_cast<int>(std::size(kQualities))))
            settings_.quality = static_cast<Quality>(q);
    }

    label("Range");
    if (ImGui::RadioButton("Entire composition", !customRange_)) customRange_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Custom", customRange_)) customRange_ = true;
    if (customRange_) {
        ImGui::SetCursorPosX(130.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        float range[2] = {rangeStart_, rangeEnd_};
        if (ImGui::DragFloat2("##range", range, 0.02f, 0.0f, static_cast<float>(comp.duration), "%.2f s",
                              ImGuiSliderFlags_AlwaysClamp)) {
            rangeStart_ = std::min(range[0], range[1]);
            rangeEnd_ = std::max(range[0], range[1]);
        }
    }

    if (formatSupportsAlpha(settings_.format)) {
        label("Background");
        ImGui::Checkbox("Transparent (no composition background)", &settings_.transparent);
    }

    label("Save to");
    std::string path = platform::toUtf8(settings_.output);
    ImGui::SetNextItemWidth(-96);
    if (ImGui::InputText("##output", &path)) settings_.output = platform::fromUtf8(path);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(-FLT_MIN, 0))) {
        if (auto file = platform::saveFileDialog("Export to", settings_.output, formatLabel(settings_.format),
                                                 formatExtension(settings_.format)))
            settings_.output = *file;
    }
    if (settings_.format == Format::PngSequence) {
        ImGui::SetCursorPosX(130.0f);
        ImGui::TextColored(theme::vec(theme::kTextFaint), "Frames are saved as %s_0000.png, _0001.png, ...",
                           platform::toUtf8(settings_.output.stem()).c_str());
    }

    if (!error_.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(theme::kDanger), "%s", error_.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 10));
    const float bw = 120.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 24 - bw * 2 - 8);
    if (ImGui::Button("Cancel", ImVec2(bw, 34)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) stage_ = Stage::Closed;
    ImGui::SameLine(0, 8);
    if (ui::accentButton("Export", ImVec2(bw, 34))) startExport(ctx);
}

void ExportDialog::startExport(EditorContext& ctx) {
    ctx.playing = false;
    settings_.start = customRange_ ? rangeStart_ : 0.0;
    settings_.end = customRange_ ? rangeEnd_ : ctx.comp().duration;
    if (settings_.output.empty() || settings_.output.filename().empty()) {
        error_ = "Choose where to save the export.";
        return;
    }
    if (settings_.output.extension() != exporting::formatExtension(settings_.format))
        settings_.output += exporting::formatExtension(settings_.format);
    error_.clear();
    if (!Fonts::ensureLoaded(ctx.comp())) {
        stage_ = Stage::Preparing;
        return;
    }
    if (!job_.start(ctx.project, settings_, error_)) {
        stage_ = Stage::Settings;
        return;
    }
    stage_ = Stage::Running;
}

void ExportDialog::drawProgress() {
    job_.step(1.0 / 30.0); // keep the UI responsive
    if (!job_.running()) {
        stage_ = Stage::Done;
        return;
    }
    const int done = job_.framesDone(), total = std::max(1, job_.framesTotal());
    const float fraction = static_cast<float>(done) / total;

    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 1.1f);
    ImGui::TextUnformatted("Exporting...");
    ImGui::PopFont();
    ImGui::TextColored(theme::vec(theme::kTextDim), "%s", platform::toUtf8(job_.settings().output.filename()).c_str());
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::kAccent);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f%%", fraction * 100.0f);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 22), overlay);
    ImGui::PopStyleColor();

    const double elapsed = job_.elapsedSeconds();
    const double remaining = done > 0 ? elapsed / done * (total - done) : 0.0;
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Frame %d of %d  ·  %s elapsed  ·  about %s left", done, total,
                       formatDuration(elapsed).c_str(), done > 0 ? formatDuration(remaining).c_str() : "--:--");

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 24 - 120);
    if (ImGui::Button("Cancel", ImVec2(120, 34))) {
        job_.cancel();
        stage_ = Stage::Done;
    }
}

void ExportDialog::drawResult() {
    const auto& output = job_.settings().output;
    if (job_.succeeded()) {
        ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 1.1f);
        ImGui::TextColored(theme::vec(theme::kAccent), "Export complete");
        ImGui::PopFont();
        ImGui::TextColored(theme::vec(theme::kTextDim), "%d frames in %s", job_.framesTotal(),
                           formatDuration(job_.elapsedSeconds()).c_str());
        ImGui::TextWrapped("%s", platform::toUtf8(output).c_str());
    } else if (job_.cancelled()) {
        ImGui::TextUnformatted("Export cancelled.");
    } else {
        ImGui::TextColored(theme::vec(theme::kDanger), "Export failed");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(job_.error().c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 10));
    const float bw = 130.0f;
    const bool ok = job_.succeeded();
    const bool isSequence = job_.settings().format == exporting::Format::PngSequence;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 24 - bw * (ok ? (isSequence ? 2 : 3) : 1) - 8 * (ok ? 2 : 0));
    if (ok) {
        if (ImGui::Button("Show in Folder", ImVec2(bw, 34)))
            platform::revealInFileManager(
                isSequence ? output.parent_path() / platform::fromUtf8(platform::toUtf8(output.stem()) + "_0000.png")
                           : output);
        ImGui::SameLine(0, 8);
        if (!isSequence) {
            if (ImGui::Button("Play", ImVec2(bw, 34))) platform::openWithDefaultApp(output);
            ImGui::SameLine(0, 8);
        }
    }
    if (ui::accentButton("Done", ImVec2(bw, 34)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) stage_ = Stage::Closed;
}

} // namespace bemotion
