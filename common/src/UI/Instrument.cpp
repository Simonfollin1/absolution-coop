#include "UI/Instrument.h"

namespace
{
    // The recessed surface and its edge. Deliberately darker than the ImGui
    // window background rather than lighter: a display reads as a hole in the
    // panel, a button reads as something raised out of it.
    constexpr ImU32 SURFACE = IM_COL32(19, 19, 19, 255);
    constexpr ImU32 EDGE    = IM_COL32(43, 43, 43, 255);
    constexpr ImU32 CAPTION = IM_COL32(143, 169, 196, 255);
    constexpr ImU32 VALUE   = IM_COL32(230, 230, 230, 255);

    constexpr float INSET   = 8.f;    // how far in from the panel edge
    constexpr float PADDING = 10.f;   // inside the frame
    constexpr float HEADER  = 20.f;   // caption strip

    ImVec2 g_origin;
    ImVec2 g_size;
    bool   g_open = false;
}

bool UI::BeginInstrument(const char* caption, float height, const char* valueText)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = available - INSET * 2.f;

    if (width <= 1.f)
    {
        g_open = false;
        return false;
    }

    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::Indent(INSET);

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), SURFACE, 3.f);
    drawList->AddRect(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), EDGE, 3.f);

    if (caption)
    {
        drawList->AddText(ImVec2(topLeft.x + PADDING, topLeft.y + 4.f), CAPTION, caption);
    }

    if (valueText)
    {
        // Always the same corner. Someone looking for the number should never
        // have to hunt for where this particular instrument decided to put it.
        const ImVec2 textSize = ImGui::CalcTextSize(valueText);

        drawList->AddText(
            ImVec2(topLeft.x + width - PADDING - textSize.x, topLeft.y + 4.f),
            VALUE, valueText);
    }

    g_origin = ImVec2(topLeft.x + PADDING, topLeft.y + HEADER);
    g_size   = ImVec2(width - PADDING * 2.f, height - HEADER - PADDING * 0.5f);
    g_open   = true;

    // Reserve the space so anything drawn after this lands below the frame.
    ImGui::Dummy(ImVec2(width, height));

    return true;
}

void UI::EndInstrument()
{
    if (g_open)
    {
        ImGui::Unindent(INSET);
        g_open = false;
    }
}

ImVec2 UI::InstrumentOrigin()
{
    return g_origin;
}

ImVec2 UI::InstrumentSize()
{
    return g_size;
}

void UI::BeginInstrumentControls()
{
    ImGui::Indent(INSET);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 3.f));
}

void UI::EndInstrumentControls()
{
    ImGui::PopStyleVar();
    ImGui::Unindent(INSET);
    ImGui::Dummy(ImVec2(0.f, 4.f));
}
