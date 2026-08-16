#pragma once

#include <imgui.h>

#include "Game/HudZones.h"

class SettingsStore;

namespace UI
{
    // Free placement for a mod's on-screen overlays.
    //
    // Split the way SettingsStore is: the mechanism lives here and is
    // mod-agnostic, while which slots exist is the mod's business. A mod
    // registers its panels once, then draws each through Begin/End and gets
    // placement, anchoring, persistence, dragging and clash reporting for free.
    //
    // Positions are stored as a fraction of the screen with a corner anchor, so
    // a layout survives a resolution change rather than drifting off the edge.
    // The pixel offset is a nudge from that anchor, which is the number people
    // actually want to type.
    class HudLayout
    {
    public:
        static constexpr int MAX_SLOTS = 10;
        static constexpr int MAX_ZONES = 8;

        enum Corner
        {
            TopLeft = 0,
            TopRight,
            BottomLeft,
            BottomRight,
            CornerCount
        };

        // Registers a placeable panel. Returns its index, or -1 when full.
        // key must be stable - it names the setting.
        int Add(const char* key, const char* label, Corner corner, float offsetX, float offsetY);

        void RegisterSettings(SettingsStore& settings, const char* prefix);

        // Where the game's own HUD is, so placement can avoid it and the map can
        // draw it. Copied, not referenced.
        void SetOccupiedZones(const Game::Zone* zones, int count);

        // Draws one slot. Returns false when the slot is switched off or the
        // window is clipped - skip the contents, but End must still be called,
        // same contract as ImGui::Begin.
        bool Begin(int slot);
        void End();

        // Which panels are on screen: one labelled checkbox each, and nothing
        // else. Kept separate from RenderPanel so it can be put where the eye
        // lands first - this is the switch people look for, and burying it in
        // the placement table made the overlays look broken.
        void RenderVisibilityList();

        // The editor: the screen map, the element table and the arrangements.
        void RenderPanel();

        // Leaves edit mode. Worth calling when the menu closes or the overlays
        // are suppressed: an editor you cannot reach must not stay switched on.
        void StopEditing() { m_editing = false; }

        // True while the player is dragging overlays on the real screen. Callers
        // should draw every slot in this state, switched on or not - something
        // you cannot see is something you cannot place.
        bool IsEditing() const { return m_editing; }

    private:
        struct Slot
        {
            char        key[24] = {};
            const char* label = "";
            int         corner = TopLeft;
            float       offsetX = 16.f;   // pixels from the anchor
            float       offsetY = 16.f;
            float       scale = 1.f;
            float       opacity = 0.85f;
            bool        visible = false;

            // Measured last frame, for the map and for clash reporting.
            ImVec2 lastPos{};
            ImVec2 lastSize{};
            bool   measured = false;
            int    clashZone = -1;
        };

        ImVec2 AnchorFor(const Slot& slot, const ImVec2& display) const;
        void   UpdateClashes();
        void   RenderMap();
        void   RenderTable();
        void   ArrangeClearOfGameHud();

        Slot m_slots[MAX_SLOTS];
        int  m_count = 0;

        Game::Zone m_zones[MAX_ZONES];
        int        m_zoneCount = 0;

        int  m_current = -1;      // slot being drawn between Begin and End
        int  m_selected = 0;
        int  m_mapDragSlot = -1;  // chip grabbed in the map, held for the gesture
        bool m_editing = false;
        bool m_snap = true;

        // Begin can bail out before it touches ImGui at all, and End is called
        // either way. These record what Begin actually did so End unwinds
        // exactly that much and no more - an unbalanced ImGui stack does not
        // fail politely.
        bool m_begun = false;
        bool m_beganOpen = false;
        bool m_beganEditing = false;
    };
}
