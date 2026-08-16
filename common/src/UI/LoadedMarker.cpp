#include "UI/LoadedMarker.h"
#include "UI/Help.h"

#include <cstdio>
#include <string>

#include <imgui.h>

#include <Global.h>
#include <Glacier/UI/ZScaleformManager.h>

namespace
{
    constexpr float MARGIN      = 16.f;
    constexpr float LINE_HEIGHT = 20.f;

    const ImVec4 COLOR_NAME   = ImVec4(0.88f, 0.88f, 0.88f, 1.f);
    const ImVec4 COLOR_STATE  = ImVec4(0.45f, 0.85f, 0.50f, 1.f);
    const ImVec4 COLOR_CREDIT = ImVec4(0.95f, 0.72f, 0.20f, 1.f);
}

void LoadedMarker::Initialize(const char* modName, int defaultSlot)
{
    m_modName = modName;
    m_slot    = defaultSlot;
}

void LoadedMarker::RegisterSettings(SettingsStore& settings, const char* keyPrefix)
{
    const std::string prefix(keyPrefix);

    settings.Bind((prefix + ".alsoInModMenu").c_str(), m_alsoInModMenu);
    settings.Bind((prefix + ".corner").c_str(),        m_corner);
    settings.Bind((prefix + ".slot").c_str(),          m_slot);
    settings.Bind((prefix + ".opacity").c_str(),       m_opacity);
}

bool LoadedMarker::IsInMainMenu()
{
    return ScaleformManager && ScaleformManager->IsInMainMenu();
}

void LoadedMarker::OnDrawUI(bool modMenuVisible)
{
    if (!IsInMainMenu() && !(m_alsoInModMenu && modMenuVisible))
    {
        return;
    }

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoMove             |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs;          // never steal a click from the menu

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float stackOffset = m_slot * LINE_HEIGHT;

    ImVec2 position;
    ImVec2 pivot;

    switch (m_corner)
    {
    case 0: position = ImVec2(MARGIN, MARGIN + stackOffset);                                pivot = ImVec2(0.f, 0.f); break;
    case 1: position = ImVec2(displaySize.x - MARGIN, MARGIN + stackOffset);                pivot = ImVec2(1.f, 0.f); break;
    case 2: position = ImVec2(MARGIN, displaySize.y - MARGIN - stackOffset);                pivot = ImVec2(0.f, 1.f); break;
    case 3:
    default: position = ImVec2(displaySize.x - MARGIN, displaySize.y - MARGIN - stackOffset); pivot = ImVec2(1.f, 1.f); break;
    }

    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.f);  // text only, no panel behind it

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6.f, 0.f));

    char windowId[64];
    std::snprintf(windowId, sizeof(windowId), "##LoadedMarker_%s", m_modName);

    if (ImGui::Begin(windowId, nullptr, flags))
    {
        ImVec4 name   = COLOR_NAME;
        ImVec4 state  = COLOR_STATE;
        ImVec4 credit = COLOR_CREDIT;
        name.w   = m_opacity;
        state.w  = m_opacity;
        credit.w = m_opacity;

        ImGui::TextColored(name, "%s", m_modName);
        ImGui::SameLine();
        ImGui::TextColored(state, "loaded");
        ImGui::SameLine();
        ImGui::TextColored(name, "%s", "·");
        ImGui::SameLine();
        ImGui::TextColored(credit, "by FOLL1N");
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

void LoadedMarker::RenderSettings()
{
    ImGui::SeparatorText("Loaded marker");

    ImGui::TextDisabled("Shown on the main menu. Placement is adjustable.");

    UI::Checkbox("Also while the mod menu is open", &m_alsoInModMenu,
        "Off by default so the marker stays out of gameplay and recordings.");

    ImGui::Combo("Corner", &m_corner, "Top left\0Top right\0Bottom left\0Bottom right\0");
    ImGui::SliderInt("Stack slot", &m_slot, 0, 4);
    UI::Help("Different slots stack multiple mods.");
    ImGui::SliderFloat("Opacity", &m_opacity, 0.35f, 1.f, "%.2f");
}
