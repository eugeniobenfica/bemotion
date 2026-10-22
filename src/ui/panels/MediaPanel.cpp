#include "ui/panels/MediaPanel.h"

#include "app/Platform.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/ViewportPanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace bemotion {

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool matches(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(filter)) != std::string::npos;
}

int layersUsing(const Composition& comp, uint64_t mediaId) {
    int n = 0;
    for (const Layer& l : comp.layers) n += l.type == LayerType::Media && l.media.mediaId == mediaId;
    return n;
}

// "Video · 1920×1080 · 0:00:12:05 · 30 fps"
std::string describe(const MediaItem& item, double fps) {
    std::string s = mediaKindLabel(item.kind);
    char buf[64];
    if (item.kind != MediaKind::Audio && item.width > 0) {
        std::snprintf(buf, sizeof buf, "  ·  %d×%d", item.width, item.height);
        s += buf;
    }
    if (item.kind != MediaKind::Image) s += "  ·  " + ui::formatTimecode(item.duration, fps);
    if (item.kind == MediaKind::Video && item.fps > 0) {
        std::snprintf(buf, sizeof buf, "  ·  %.4g fps", item.fps);
        s += buf;
    }
    return s;
}

// The thumbnail box: the picture fitted inside, or the kind's icon.
void drawThumbnail(ImDrawList* dl, const MediaItem& item, ImVec2 a, ImVec2 b, bool missing) {
    dl->AddRectFilled(a, b, theme::kBgDeep, 4.0f);
    const ImTextureID tex = missing || item.kind == MediaKind::Audio ? ImTextureID{} : media::MediaManager::get().thumbnail(item);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    if (tex && item.width > 0 && item.height > 0) {
        const float boxW = b.x - a.x, boxH = b.y - a.y;
        const float s = std::min(boxW / item.width, boxH / item.height);
        const ImVec2 half(item.width * s * 0.5f, item.height * s * 0.5f);
        dl->AddImage(tex, ImVec2(c.x - half.x, c.y - half.y), ImVec2(c.x + half.x, c.y + half.y));
    } else {
        ui::drawIcon(dl, ui::iconForMedia(item.kind), c, (b.y - a.y) * 0.42f, missing ? theme::kDanger : theme::kTextDim);
    }
    dl->AddRect(a, b, missing ? theme::kDanger : theme::kBorder, 4.0f);
}

void dragSource(const MediaItem& item) {
    if (!ImGui::BeginDragDropSource()) return;
    ImGui::SetDragDropPayload(kMediaPayload, &item.id, sizeof(item.id));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(s, s));
    ui::drawIcon(ImGui::GetWindowDrawList(), ui::iconForMedia(item.kind), ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.6f,
                 theme::kAccent);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  ·  drop on the canvas or the timeline", item.name.c_str());
    ImGui::EndDragDropSource();
}

} // namespace

void MediaPanel::importFiles(EditorContext& ctx, const std::vector<std::filesystem::path>& files, bool addLayers) {
    if (files.empty()) return;
    std::vector<std::string> errors;
    const std::vector<uint64_t> ids = media::MediaManager::get().import(ctx.comp(), files, &errors);
    if (addLayers)
        for (uint64_t id : ids) ctx.addMedia(id);
    if (!ids.empty()) {
        selected_ = ids.back();
        ctx.touch();
    }
    message_.clear();
    for (const std::string& e : errors) message_ += (message_.empty() ? "" : "\n") + e;
}

void MediaPanel::openImportDialog(EditorContext& ctx) {
    importFiles(ctx, platform::openFilesDialog("Import Media", "Media files", media::supportedExtensions()), false);
}

void MediaPanel::draw(EditorContext& ctx) {
    hovered_ = false;
    if (!open || ui::panelHidden(kWindowName)) return; // minimized: only its restore handle shows
    // New in this version: existing layouts get the tab next to Objects.
    if (ImGuiWindow* objects = ImGui::FindWindowByName(ObjectsPanel::kWindowName); objects && objects->DockId)
        ImGui::SetNextWindowDockID(objects->DockId, ImGuiCond_FirstUseEver);
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open)) {
        ImGui::End();
        return;
    }
    hovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (ui::panelIsMinimized()) {
        ui::panelMinimizeButton();
        ImGui::End();
        return;
    }

    Composition& comp = ctx.comp();
    if (ui::iconButton("##import", ui::Icon::Import, ImGui::GetFrameHeight(), "Import images, videos or audio (Ctrl+I)"))
        openImportDialog(ctx);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search media", &search_);

    if (!message_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
        ImGui::TextWrapped("%s", message_.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Dismiss")) message_.clear();
    }

    if (comp.media.empty()) {
        ImGui::Dummy(ImVec2(0, 12));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
        ImGui::TextWrapped("No media yet. Import images, videos or audio, or drop files onto the window.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6));
        if (ui::accentButton("Import...", ImVec2(-FLT_MIN, 0))) openImportDialog(ctx);
    } else if (ImGui::BeginChild("##mediaList", ImVec2(0, 0), ImGuiChildFlags_None)) {
        const float rowH = std::max(44.0f, ImGui::GetTextLineHeight() * 2.6f);
        // Iterate by id: the context menu may remove items.
        std::vector<uint64_t> ids;
        for (const MediaItem& item : comp.media)
            if (matches(item.name, search_)) ids.push_back(item.id);
        for (uint64_t id : ids)
            if (MediaItem* item = comp.findMedia(id)) drawItem(ctx, *item, rowH);
        if (ids.empty()) ImGui::TextDisabled("Nothing matches \"%s\".", search_.c_str());
    }
    if (!comp.media.empty()) ImGui::EndChild();

    drawRemoveModal(ctx);
    ui::panelMinimizeButton();
    ImGui::End();
}

void MediaPanel::drawItem(EditorContext& ctx, MediaItem& item, float rowH) {
    media::MediaManager& mm = media::MediaManager::get();
    const bool missing = mm.fileMissing(item);
    const std::string error = missing ? "File not found: " + item.path : mm.error(item.id);

    ImGui::PushID(static_cast<int>(item.id));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable("##row", selected_ == item.id, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(w, rowH))) {
        selected_ = item.id;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !missing) ctx.addMedia(item.id);
    }
    if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(item.name.c_str());
        ImGui::TextDisabled("%s", item.path.c_str());
        if (!error.empty()) ImGui::TextColored(theme::vec(theme::kDanger), "%s", error.c_str());
        else ImGui::TextDisabled("Drag onto the canvas or the timeline, or double-click to add at the playhead.");
        ImGui::EndTooltip();
    }
    if (!missing) dragSource(item);
    if (ImGui::BeginPopupContextItem("##menu")) {
        selected_ = item.id;
        drawItemMenu(ctx, item);
        ImGui::EndPopup();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float thumbH = rowH - 8.0f, thumbW = thumbH * 16.0f / 9.0f;
    drawThumbnail(dl, item, ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + 4 + thumbW, p.y + 4 + thumbH), missing);
    const float tx = p.x + thumbW + 12.0f;
    const float lh = ImGui::GetTextLineHeight();
    const float ty = p.y + (rowH - lh * 2.0f - 2.0f) * 0.5f;
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(p.x + w - 4, p.y + rowH), true);
    dl->AddText(ImVec2(tx, ty), missing ? theme::kDanger : theme::kText, item.name.c_str());
    std::string info = missing ? "Missing — right-click to relink" : describe(item, ctx.comp().fps);
    if (!missing && item.hasAudio && mm.audioPending(item.id)) info += "  ·  reading audio...";
    else if (!missing && !error.empty()) info = "Error — " + error;
    dl->AddText(ImVec2(tx, ty + lh + 2.0f), !error.empty() ? theme::kDanger : theme::kTextFaint, info.c_str());
    dl->PopClipRect();
    if (const int uses = layersUsing(ctx.comp(), item.id); uses > 0) { // used in the composition: a small dot, like AE's
        dl->AddCircleFilled(ImVec2(p.x + w - 10, p.y + 10), 3.0f, theme::kAccent);
    }
    ImGui::PopID();
}

void MediaPanel::drawItemMenu(EditorContext& ctx, MediaItem& item) {
    const bool missing = media::MediaManager::get().fileMissing(item);
    if (ImGui::MenuItem("Add to Composition", nullptr, false, !missing)) ctx.addMedia(item.id);
    if (ImGui::MenuItem("Show in Folder", nullptr, false, !missing)) platform::revealInFileManager(pathOf(item.path));
    if (ImGui::MenuItem(missing ? "Relink..." : "Replace Footage...")) relink(ctx, item);
    ImGui::Separator();
    if (ImGui::MenuItem("Remove")) {
        removing_ = item.id;
        if (layersUsing(ctx.comp(), item.id) > 0) openRemoveModal_ = true;
    }
    // Unused items go right away; used ones ask first (their layers go too).
    if (removing_ && !openRemoveModal_) {
        auto& list = ctx.comp().media;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const MediaItem& m) { return m.id == removing_; }), list.end());
        removing_ = 0;
        ctx.touch();
    }
}

void MediaPanel::relink(EditorContext& ctx, MediaItem& item) {
    const auto files = platform::openFilesDialog("Relink Media", "Media files", media::supportedExtensions());
    if (files.empty()) return;
    media::ProbeResult r = media::probe(files.front());
    if (!r.ok) {
        message_ = r.item.name + ": " + r.error;
        return;
    }
    // Same item (its layers keep pointing at it), new file.
    r.item.id = item.id;
    item = std::move(r.item);
    for (Layer& l : ctx.comp().layers)
        if (l.type == LayerType::Media && l.media.mediaId == item.id) {
            l.media.hasVideo = item.hasVideo;
            l.media.hasAudio = item.hasAudio;
            if (item.width > 0) l.media.size = {static_cast<float>(item.width), static_cast<float>(item.height)};
        }
    message_.clear();
    ctx.touch();
}

void MediaPanel::drawRemoveModal(EditorContext& ctx) {
    constexpr const char* kId = "Remove Media?";
    if (openRemoveModal_) {
        ImGui::OpenPopup(kId);
        openRemoveModal_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    Composition& comp = ctx.comp();
    const MediaItem* item = comp.findMedia(removing_);
    if (!item) {
        removing_ = 0;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const int uses = layersUsing(comp, item->id);
    ImGui::Text("\"%s\" is used by %d layer%s.", item->name.c_str(), uses, uses == 1 ? "" : "s");
    ImGui::TextDisabled("Removing it also deletes %s.", uses == 1 ? "that layer" : "those layers");
    ImGui::Dummy(ImVec2(0, 6));
    if (ui::accentButton("Remove", ImVec2(110, 0))) {
        std::vector<uint64_t> doomed;
        for (const Layer& l : comp.layers)
            if (l.type == LayerType::Media && l.media.mediaId == removing_) doomed.push_back(l.id);
        for (uint64_t id : doomed) comp.removeLayerTree(id);
        comp.media.erase(std::remove_if(comp.media.begin(), comp.media.end(),
                                        [&](const MediaItem& m) { return m.id == removing_; }),
                         comp.media.end());
        ctx.select(0);
        ctx.touch();
        removing_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        removing_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace bemotion
