#include "UI/HudLayout.h"
#include "UI/Help.h"
#include "Config/SettingsStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr ImU32 MAP_BACKGROUND = IM_COL32( 16,  14,  11, 255);
    constexpr ImU32 MAP_EDGE       = IM_COL32( 58,  58,  58, 255);
    constexpr ImU32 ZONE_FILL      = IM_COL32(138,  64,  56,  60);
    constexpr ImU32 ZONE_EDGE      = IM_COL32(138,  64,  56, 200);
    constexpr ImU32 ZONE_TEXT      = IM_COL32(224, 138, 128, 255);
    constexpr ImU32 CHIP_FILL      = IM_COL32( 14,  26,  36, 235);
    constexpr ImU32 CHIP_EDGE      = IM_COL32( 79, 163, 227, 255);
    constexpr ImU32 CHIP_SELECTED  = IM_COL32(242, 184,  51, 255);
    constexpr ImU32 GUIDE          = IM_COL32(242, 184,  51, 110);

    constexpr float SNAP_PIXELS = 12.f;

    // The map is drawn 16:9 regardless of the real aspect. Positions are
    // fractions, so the map is a diagram of proportions rather than a
    // screenshot, and a fixed shape keeps it readable in a narrow panel.
    constexpr float MAP_ASPECT = 16.f / 9.f;
}

int UI::HudLayout::Add(const char* key, const char* label, Corner corner, float offsetX, float offsetY)
{
    if (m_count >= MAX_SLOTS || !key)
    {
        return -1;
    }

    Slot& slot = m_slots[m_count];

    std::snprintf(slot.key, sizeof(slot.key), "%s", key);

    slot.label   = label ? label : key;
    slot.corner  = corner;
    slot.offsetX = offsetX;
    slot.offsetY = offsetY;

    return m_count++;
}

void UI::HudLayout::RegisterSettings(SettingsStore& settings, const char* prefix)
{
    for (int i = 0; i < m_count; ++i)
    {
        Slot& slot = m_slots[i];

        // Keyed by the slot's name rather than its index, so adding a panel
        // later cannot silently reassign someone's saved layout.
        char key[96];

        std::snprintf(key, sizeof(key), "%s.%s.corner", prefix, slot.key);
        settings.Bind(key, slot.corner);

        std::snprintf(key, sizeof(key), "%s.%s.x", prefix, slot.key);
        settings.Bind(key, slot.offsetX);

        std::snprintf(key, sizeof(key), "%s.%s.y", prefix, slot.key);
        settings.Bind(key, slot.offsetY);

        std::snprintf(key, sizeof(key), "%s.%s.scale", prefix, slot.key);
        settings.Bind(key, slot.scale);

        std::snprintf(key, sizeof(key), "%s.%s.opacity", prefix, slot.key);
        settings.Bind(key, slot.opacity);

        std::snprintf(key, sizeof(key), "%s.%s.on", prefix, slot.key);
        settings.Bind(key, slot.visible);
    }
}

void UI::HudLayout::SetOccupiedZones(const Game::Zone* zones, int count)
{
    m_zoneCount = std::clamp(count, 0, MAX_ZONES);

    for (int i = 0; i < m_zoneCount; ++i)
    {
        m_zones[i] = zones[i];
    }
}

ImVec2 UI::HudLayout::AnchorFor(const Slot& slot, const ImVec2& display) const
{
    switch (slot.corner)
    {
    case TopRight:    return ImVec2(display.x - slot.offsetX, slot.offsetY);
    case BottomLeft:  return ImVec2(slot.offsetX,             display.y - slot.offsetY);
    case BottomRight: return ImVec2(display.x - slot.offsetX, display.y - slot.offsetY);
    case TopLeft:
    default:          return ImVec2(slot.offsetX,             slot.offsetY);
    }
}

bool UI::HudLayout::Begin(int slot)
{
    m_begun        = false;
    m_beganOpen    = false;
    m_beganEditing = false;
    m_current      = -1;

    if (slot < 0 || slot >= m_count)
    {
        return false;
    }

    Slot& s = m_slots[slot];

    // While editing, everything is drawn whether or not it is switched on.
    // Something you cannot see is something you cannot place.
    if (!s.visible && !m_editing)
    {
        return false;
    }

    m_current = slot;

    // Captured here rather than read again in End: the two must agree about
    // how many style entries were pushed, whatever else happens this frame.
    m_beganEditing = m_editing;

    const ImVec2 display = ImGui::GetIO().DisplaySize;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoCollapse        |
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (!m_beganEditing)
    {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    }

    const ImVec2 pivot(
        (s.corner == TopRight || s.corner == BottomRight) ? 1.f : 0.f,
        (s.corner == BottomLeft || s.corner == BottomRight) ? 1.f : 0.f);

    // Editing hands the position to ImGui and reads it back; otherwise we own
    // it outright and reassert it every frame.
    ImGui::SetNextWindowPos(AnchorFor(s, display),
        m_beganEditing ? ImGuiCond_Once : ImGuiCond_Always, pivot);

    ImGui::SetNextWindowBgAlpha(m_beganEditing ? 0.55f : s.opacity);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));

    if (m_beganEditing)
    {
        ImGui::PushStyleColor(ImGuiCol_Border,
            slot == m_selected ? ImVec4(0.95f, 0.72f, 0.20f, 1.f)
                               : ImVec4(0.31f, 0.64f, 0.89f, 0.8f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    }

    char windowId[48];
    std::snprintf(windowId, sizeof(windowId), "##hud_%s", s.key);

    m_begun = true;

    const bool open = ImGui::Begin(windowId, nullptr, flags);

    m_beganOpen = open;

    if (open)
    {
        ImGui::SetWindowFontScale(s.scale);
    }

    return open;
}

void UI::HudLayout::End()
{
    // Begin can return before it calls ImGui::Begin - a slot that is switched
    // off, or an index that does not exist. Unwinding in that case would pop a
    // window we never pushed.
    if (!m_begun)
    {
        return;
    }

    if (m_current >= 0 && m_beganOpen)
    {
        Slot& s = m_slots[m_current];

        s.lastPos  = ImGui::GetWindowPos();
        s.lastSize = ImGui::GetWindowSize();
        s.measured = true;

        if (m_beganEditing)
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;

            // Convert the dragged position back into an offset from whichever
            // corner this slot is anchored to, so the stored form never depends
            // on where it happens to be sitting right now.
            const float right  = s.lastPos.x + s.lastSize.x;
            const float bottom = s.lastPos.y + s.lastSize.y;

            float x = (s.corner == TopRight || s.corner == BottomRight)
                ? display.x - right : s.lastPos.x;
            float y = (s.corner == BottomLeft || s.corner == BottomRight)
                ? display.y - bottom : s.lastPos.y;

            if (m_snap)
            {
                if (std::fabs(x - 16.f) < SNAP_PIXELS) x = 16.f;
                if (std::fabs(y - 16.f) < SNAP_PIXELS) y = 16.f;
            }

            s.offsetX = x;
            s.offsetY = y;

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_selected = m_current;
            }
        }

        ImGui::SetWindowFontScale(1.f);
    }

    ImGui::End();

    if (m_beganEditing)
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleVar(2);

    m_begun   = false;
    m_current = -1;
}

void UI::HudLayout::UpdateClashes()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    if (display.x <= 1.f || display.y <= 1.f)
    {
        return;
    }

    for (int i = 0; i < m_count; ++i)
    {
        Slot& s = m_slots[i];

        s.clashZone = -1;

        if (!s.measured || !s.visible)
        {
            continue;
        }

        const float left   = s.lastPos.x / display.x;
        const float top    = s.lastPos.y / display.y;
        const float right  = (s.lastPos.x + s.lastSize.x) / display.x;
        const float bottom = (s.lastPos.y + s.lastSize.y) / display.y;

        for (int z = 0; z < m_zoneCount; ++z)
        {
            const Game::Zone& zone = m_zones[z];

            const bool overlaps =
                left   < zone.x + zone.width  &&
                right  > zone.x               &&
                top    < zone.y + zone.height &&
                bottom > zone.y;

            if (overlaps)
            {
                s.clashZone = z;
                break;
            }
        }
    }
}

void UI::HudLayout::ArrangeClearOfGameHud()
{
    // Not a preset of positions: it walks candidate spots in each corner and
    // takes the first that does not land on an occupied zone, so it adapts to
    // whatever is currently on screen.
    struct Candidate { Corner corner; float x; float y; };

    constexpr Candidate CANDIDATES[] = {
        { TopLeft,     16.f,  16.f },
        { TopLeft,     16.f, 120.f },
        { TopRight,    16.f,  16.f },
        { TopRight,    16.f, 120.f },
        { TopLeft,     16.f, 224.f },
        { TopRight,    16.f, 224.f },
        { BottomRight, 16.f, 120.f },
        { BottomLeft,  16.f, 240.f },
        { BottomRight, 16.f, 240.f },
        { TopLeft,     16.f, 328.f },
    };

    constexpr int CANDIDATE_COUNT = static_cast<int>(sizeof(CANDIDATES) / sizeof(CANDIDATES[0]));

    const ImVec2 display = ImGui::GetIO().DisplaySize;

    if (display.x <= 1.f || display.y <= 1.f)
    {
        return;
    }

    bool taken[CANDIDATE_COUNT] = {};

    for (int i = 0; i < m_count; ++i)
    {
        Slot& s = m_slots[i];

        if (!s.visible)
        {
            continue;
        }

        // Assume a modest size when nothing has been measured yet, so a first
        // run still lands somewhere sensible.
        const float width  = (s.measured ? s.lastSize.x : 180.f) / display.x;
        const float height = (s.measured ? s.lastSize.y : 60.f)  / display.y;

        for (int c = 0; c < CANDIDATE_COUNT; ++c)
        {
            if (taken[c])
            {
                continue;
            }

            const Candidate& candidate = CANDIDATES[c];

            const float left = (candidate.corner == TopRight || candidate.corner == BottomRight)
                ? 1.f - (candidate.x / display.x) - width
                : candidate.x / display.x;

            const float top = (candidate.corner == BottomLeft || candidate.corner == BottomRight)
                ? 1.f - (candidate.y / display.y) - height
                : candidate.y / display.y;

            bool clear = true;

            for (int z = 0; z < m_zoneCount && clear; ++z)
            {
                const Game::Zone& zone = m_zones[z];

                clear = !(left < zone.x + zone.width &&
                          left + width > zone.x      &&
                          top  < zone.y + zone.height &&
                          top + height > zone.y);
            }

            if (clear)
            {
                s.corner  = candidate.corner;
                s.offsetX = candidate.x;
                s.offsetY = candidate.y;
                taken[c]  = true;
                break;
            }
        }
    }
}

void UI::HudLayout::RenderMap()
{
    const float width  = std::max(ImGui::GetContentRegionAvail().x - 16.f, 160.f);
    const float height = width / MAP_ASPECT;

    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##hudmap", ImVec2(width, height));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), MAP_BACKGROUND);
    draw->AddRect(origin, ImVec2(origin.x + width, origin.y + height), MAP_EDGE);

    const auto toMap = [&](float fx, float fy)
    {
        return ImVec2(origin.x + fx * width, origin.y + fy * height);
    };

    // The game's HUD first, underneath ours, because it is the constraint
    // rather than the content.
    for (int z = 0; z < m_zoneCount; ++z)
    {
        const Game::Zone& zone = m_zones[z];

        const ImVec2 topLeft = toMap(zone.x, zone.y);
        const ImVec2 bottomRight = toMap(zone.x + zone.width, zone.y + zone.height);

        draw->AddRectFilled(topLeft, bottomRight, ZONE_FILL, 2.f);
        draw->AddRect(topLeft, bottomRight, ZONE_EDGE, 2.f);

        const ImVec2 textSize = ImGui::CalcTextSize(zone.name);

        if (textSize.x < bottomRight.x - topLeft.x)
        {
            draw->AddText(
                ImVec2((topLeft.x + bottomRight.x - textSize.x) * 0.5f,
                       (topLeft.y + bottomRight.y - textSize.y) * 0.5f),
                ZONE_TEXT, zone.name);
        }
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 mouse   = ImGui::GetIO().MousePos;

    // Which chip the gesture grabbed, decided once when the button goes down
    // and held for the whole drag. Hit-testing every frame instead would hand
    // the drag to whichever chip the cursor happened to cross.
    const bool grabbing = ImGui::IsItemActivated();

    // Cleared both when a fresh gesture starts and when none is running, so a
    // slot can never stay latched past the drag that grabbed it.
    if (grabbing || !ImGui::IsItemActive())
    {
        m_mapDragSlot = -1;
    }

    for (int i = 0; i < m_count; ++i)
    {
        Slot& s = m_slots[i];

        if (!s.visible && !m_editing)
        {
            continue;
        }

        const float slotWidth  = (s.measured ? s.lastSize.x : 170.f) / std::max(display.x, 1.f);
        const float slotHeight = (s.measured ? s.lastSize.y : 54.f)  / std::max(display.y, 1.f);

        // Where the anchor puts it, in fractions.
        const float left = (s.corner == TopRight || s.corner == BottomRight)
            ? 1.f - (s.offsetX / std::max(display.x, 1.f)) - slotWidth
            : s.offsetX / std::max(display.x, 1.f);

        const float top = (s.corner == BottomLeft || s.corner == BottomRight)
            ? 1.f - (s.offsetY / std::max(display.y, 1.f)) - slotHeight
            : s.offsetY / std::max(display.y, 1.f);

        const ImVec2 topLeft = toMap(left, top);
        const ImVec2 bottomRight = toMap(left + slotWidth, top + slotHeight);

        const bool selected = (i == m_selected);

        draw->AddRectFilled(topLeft, bottomRight, CHIP_FILL, 3.f);
        draw->AddRect(topLeft, bottomRight, selected ? CHIP_SELECTED : CHIP_EDGE, 3.f, 0, selected ? 2.f : 1.f);

        const ImVec2 textSize = ImGui::CalcTextSize(s.label);

        if (textSize.x < bottomRight.x - topLeft.x - 4.f)
        {
            draw->AddText(ImVec2(topLeft.x + 4.f, topLeft.y + 3.f),
                selected ? CHIP_SELECTED : CHIP_EDGE, s.label);
        }

        // Dragging in the map moves the same offsets the on-screen editor does.
        const bool hovered =
            mouse.x >= topLeft.x && mouse.x <= bottomRight.x &&
            mouse.y >= topLeft.y && mouse.y <= bottomRight.y;

        // Later chips are drawn over earlier ones, so the last match is the one
        // the player is pointing at.
        if (grabbing && hovered)
        {
            m_mapDragSlot = i;
            m_selected    = i;
        }

        if (m_mapDragSlot == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;

            const float deltaX = (delta.x / width)  * display.x;
            const float deltaY = (delta.y / height) * display.y;

            s.offsetX += (s.corner == TopRight || s.corner == BottomRight) ? -deltaX : deltaX;
            s.offsetY += (s.corner == BottomLeft || s.corner == BottomRight) ? -deltaY : deltaY;

            s.offsetX = std::max(s.offsetX, 0.f);
            s.offsetY = std::max(s.offsetY, 0.f);
        }
    }

    if (m_snap)
    {
        draw->AddLine(toMap(0.5f, 0.f), toMap(0.5f, 1.f), GUIDE);
    }
}

void UI::HudLayout::RenderVisibilityList()
{
    ImGui::SeparatorText("On screen");

    if (m_count == 0)
    {
        ImGui::TextDisabled("Nothing registered.");
        return;
    }

    int shown = 0;

    for (int i = 0; i < m_count; ++i)
    {
        ImGui::PushID(i);

        if (ImGui::Checkbox(m_slots[i].label, &m_slots[i].visible))
        {
            m_selected = i;
        }

        ImGui::PopID();

        if (m_slots[i].visible)
        {
            ++shown;
        }
    }

    if (shown == 0)
    {
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
            "Nothing is switched on, so nothing will appear on screen.");
    }
}

void UI::HudLayout::RenderTable()
{
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##hudslots", 6, flags))
    {
        return;
    }

    ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 150.f);
    ImGui::TableSetupColumn("Corner",  ImGuiTableColumnFlags_WidthFixed, 108.f);
    ImGui::TableSetupColumn("Offset",  ImGuiTableColumnFlags_WidthFixed, 130.f);
    ImGui::TableSetupColumn("Scale",   ImGuiTableColumnFlags_WidthFixed, 70.f);
    ImGui::TableSetupColumn("Opacity", ImGuiTableColumnFlags_WidthFixed, 70.f);
    ImGui::TableSetupColumn("Clash");
    ImGui::TableHeadersRow();

    for (int i = 0; i < m_count; ++i)
    {
        Slot& s = m_slots[i];

        ImGui::PushID(i);
        ImGui::TableNextRow();

        if (i == m_selected)
        {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(28, 53, 80, 255));
        }

        ImGui::TableNextColumn();

        if (ImGui::Checkbox("##on", &s.visible))
        {
            m_selected = i;
        }

        ImGui::SameLine();

        if (ImGui::Selectable(s.label, i == m_selected))
        {
            m_selected = i;
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.f);
        ImGui::Combo("##corner", &s.corner, "Top left\0Top right\0Bottom left\0Bottom right\0");

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.f);
        float offset[2] = { s.offsetX, s.offsetY };

        if (ImGui::DragFloat2("##offset", offset, 1.f, 0.f, 4000.f, "%.0f"))
        {
            s.offsetX = offset[0];
            s.offsetY = offset[1];
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.f);
        ImGui::DragFloat("##scale", &s.scale, 0.01f, 0.5f, 2.5f, "%.2f");

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.f);
        ImGui::DragFloat("##opacity", &s.opacity, 0.01f, 0.f, 1.f, "%.2f");

        ImGui::TableNextColumn();

        if (!s.visible)
        {
            ImGui::TextDisabled("off");
        }
        else if (s.clashZone >= 0)
        {
            ImGui::TextColored(ImVec4(1.f, 0.56f, 0.52f, 1.f), "%s", m_zones[s.clashZone].name);
        }
        else
        {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.f), "clear");
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void UI::HudLayout::RenderPanel()
{
    UpdateClashes();

    ImGui::SeparatorText("Layout");

    if (ImGui::Button(m_editing ? "Done" : "Edit on screen", ImVec2(160.f, 0.f)))
    {
        m_editing = !m_editing;
    }

    UI::Help("Makes the overlays draggable where they actually sit, against the "
             "real scene at the real resolution - the only way to know whether a "
             "corner is genuinely free. Everything is drawn while editing, "
             "switched on or not.");

    ImGui::SameLine();
    UI::Checkbox("Snap", &m_snap, "Snaps to the margins and the screen centre.");

    if (m_editing)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.20f, 1.f),
            "Editing - drag the overlays on screen. Amber is the selected one.");
    }

    RenderMap();

    ImGui::TextDisabled("Red is the game's own HUD. Blue is ours. Drag either view.");

    RenderTable();

    if (ImGui::Button("Clear of game HUD"))
    {
        ArrangeClearOfGameHud();
    }

    UI::Help("Places everything switched on into space the game is not using. Not "
             "a fixed arrangement - it reads the occupied zones, so hiding the "
             "minimap frees its corner and this will use it.");

    ImGui::SameLine();

    if (ImGui::Button("Stack top left"))
    {
        float y = 16.f;

        for (int i = 0; i < m_count; ++i)
        {
            if (!m_slots[i].visible)
            {
                continue;
            }

            m_slots[i].corner  = TopLeft;
            m_slots[i].offsetX = 16.f;
            m_slots[i].offsetY = y;

            y += m_slots[i].measured ? m_slots[i].lastSize.y + 6.f : 64.f;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset"))
    {
        for (int i = 0; i < m_count; ++i)
        {
            m_slots[i].corner  = TopLeft;
            m_slots[i].offsetX = 16.f;
            m_slots[i].offsetY = 16.f;
            m_slots[i].scale   = 1.f;
            m_slots[i].opacity = 0.85f;
        }
    }
}
