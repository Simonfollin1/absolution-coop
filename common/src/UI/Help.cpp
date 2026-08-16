#include "UI/Help.h"

#include <imgui.h>

namespace
{
    constexpr float TOOLTIP_WIDTH = 320.f;
}

void UI::Help(const char* description)
{
    if (!description || !*description)
    {
        return;
    }

    ImGui::SameLine();

    // A full-width control ends exactly at the content edge, and the marker
    // placed one ItemSpacing further right lands outside the window's clip
    // rect. ImGui does not complain about that - ItemAdd simply returns false,
    // nothing is drawn, and IsItemHovered stays false forever, so the tooltip
    // silently never appears. Wrapping onto the next line costs one row and is
    // always visible.
    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize("(?)").x)
    {
        ImGui::NewLine();
    }

    ImGui::TextDisabled("(?)");

    // Deliberately not using BeginTooltip()'s return value: it only became a
    // bool in ImGui 1.89.7, and the SDK bundles its own fork.
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * (TOOLTIP_WIDTH / 16.f));
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool UI::Checkbox(const char* label, bool* value, const char* description)
{
    const bool changed = ImGui::Checkbox(label, value);

    Help(description);

    return changed;
}

bool UI::SliderFloat(const char* label, float* value, float min, float max,
                     const char* format, const char* description)
{
    const bool changed = ImGui::SliderFloat(label, value, min, max, format);

    Help(description);

    return changed;
}

bool UI::SliderInt(const char* label, int* value, int min, int max,
                   const char* description)
{
    const bool changed = ImGui::SliderInt(label, value, min, max);

    Help(description);

    return changed;
}

bool UI::Button(const char* label, const char* description)
{
    const bool pressed = ImGui::Button(label);

    Help(description);

    return pressed;
}
