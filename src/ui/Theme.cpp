#include "ui/Theme.h"

namespace mirage::theme {

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 4);
    s.IndentSpacing = 14;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.WindowRounding = 8;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 4;
    s.TabRounding = 5;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.DockingSeparatorSize = 2;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = vec(kText);
    c[ImGuiCol_TextDisabled] = vec(kTextFaint);
    c[ImGuiCol_WindowBg] = vec(kBgPanel);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = vec(IM_COL32(26, 26, 30, 250));
    c[ImGuiCol_Border] = vec(kBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(kBgRaised);
    c[ImGuiCol_FrameBgHovered] = vec(kBgHover);
    c[ImGuiCol_FrameBgActive] = vec(IM_COL32(48, 48, 55, 255));
    c[ImGuiCol_TitleBg] = vec(kBgDeep);
    c[ImGuiCol_TitleBgActive] = vec(kBgDeep);
    c[ImGuiCol_TitleBgCollapsed] = vec(kBgDeep);
    c[ImGuiCol_MenuBarBg] = vec(kBgDeep);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(IM_COL32(60, 60, 68, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = vec(IM_COL32(80, 80, 90, 255));
    c[ImGuiCol_ScrollbarGrabActive] = vec(kAccent);
    c[ImGuiCol_CheckMark] = vec(kAccent);
    c[ImGuiCol_SliderGrab] = vec(kAccent);
    c[ImGuiCol_SliderGrabActive] = vec(kAccentHover);
    c[ImGuiCol_Button] = vec(kBgRaised);
    c[ImGuiCol_ButtonHovered] = vec(kBgHover);
    c[ImGuiCol_ButtonActive] = vec(IM_COL32(52, 52, 60, 255));
    c[ImGuiCol_Header] = vec(IM_COL32(255, 255, 255, 12));
    c[ImGuiCol_HeaderHovered] = vec(IM_COL32(255, 255, 255, 20));
    c[ImGuiCol_HeaderActive] = vec(IM_COL32(232, 185, 49, 50));
    c[ImGuiCol_Separator] = vec(kBorder);
    c[ImGuiCol_SeparatorHovered] = vec(kAccent);
    c[ImGuiCol_SeparatorActive] = vec(kAccent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = vec(kAccent);
    c[ImGuiCol_ResizeGripActive] = vec(kAccent);
    c[ImGuiCol_Tab] = vec(kBgDeep);
    c[ImGuiCol_TabHovered] = vec(kBgHover);
    c[ImGuiCol_TabSelected] = vec(kBgPanel);
    c[ImGuiCol_TabSelectedOverline] = vec(kAccent);
    c[ImGuiCol_TabDimmed] = vec(kBgDeep);
    c[ImGuiCol_TabDimmedSelected] = vec(kBgPanel);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = vec(IM_COL32(232, 185, 49, 90));
    c[ImGuiCol_DockingEmptyBg] = vec(kBgDeep);
    c[ImGuiCol_TextSelectedBg] = vec(IM_COL32(232, 185, 49, 70));
    c[ImGuiCol_DragDropTarget] = vec(kAccent);
    c[ImGuiCol_NavCursor] = vec(kAccent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_TableHeaderBg] = vec(kBgRaised);
    c[ImGuiCol_TableBorderLight] = vec(kBorder);
    c[ImGuiCol_TableBorderStrong] = vec(kBorder);
}

} // namespace mirage::theme
