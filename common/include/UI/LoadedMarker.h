#pragma once

#include "Config/SettingsStore.h"

// A "<mod> loaded - by FOLL1N" line in the corner of the main menu.
//
// Shown on the main menu so you can confirm at a glance that the mod is
// installed and running, and nowhere else by default - it should never sit on
// top of gameplay or anything being recorded.
//
// Neither the line nor the credit is optional. Where it sits and how visible it
// is are yours; whether it appears at all is not.
//
// Menu detection uses ZScaleformManager::IsInMainMenu(), an engine call, so it
// needs no static offsets and behaves the same on the Steam and GOG builds.
//
// Each mod draws its own line. The slot index stacks them so two installed mods
// read as one list rather than overlapping.
class LoadedMarker
{
public:
    // modName is the display name, e.g. "Speedrun Toolkit".
    void Initialize(const char* modName, int defaultSlot);

    void RegisterSettings(SettingsStore& settings, const char* keyPrefix);

    // modMenuVisible: true while the SDK panel has focus or this mod's own
    // window is open. Only used when the optional extra mode is enabled.
    void OnDrawUI(bool modMenuVisible);

    // Rendered inside the owning mod's settings UI.
    void RenderSettings();

private:
    static bool IsInMainMenu();

    const char* m_modName = "";
    bool  m_alsoInModMenu = false;
    int   m_corner        = 3;      // 0 TL, 1 TR, 2 BL, 3 BR
    int   m_slot          = 0;
    float m_opacity       = 0.6f;
};
