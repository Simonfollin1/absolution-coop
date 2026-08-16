#include "Game/HudZones.h"

namespace
{
    // Absolution's HUD, as fractions of the screen. Generous rather than tight:
    // a zone that is slightly too big pushes an overlay a little further away
    // than needed, while one that is slightly too small lets it land on top of
    // the thing it was avoiding. Only one of those two mistakes is visible.
    //
    // The index of each entry matches the HUD element list in VisualFilters, so
    // hiding an element frees exactly its zone.
    struct Entry
    {
        Game::Zone zone;
        int elementIndex;   // -1 when nothing can hide it
    };

    constexpr Entry ENTRIES[] = {
        // Minimap and threat radar, bottom left.
        { { 0.010f, 0.760f, 0.190f, 0.230f, "minimap" },           0 },
        // Weapon display, bottom right.
        { { 0.800f, 0.780f, 0.190f, 0.210f, "weapon display" },    4 },
        // Objective banner, top centre. Comes and goes, which is what makes it
        // the one people place an overlay on top of.
        { { 0.330f, 0.010f, 0.340f, 0.110f, "objective banner" }, -1 },
        // Instinct bar, bottom centre left.
        { { 0.210f, 0.900f, 0.170f, 0.090f, "instinct bar" },      3 },
        // Rating tracker, top right.
        { { 0.780f, 0.020f, 0.210f, 0.130f, "rating tracker" },    6 },
        // Crosshair, dead centre. Small, but an overlay across it is worse than
        // an overlay anywhere else.
        { { 0.460f, 0.440f, 0.080f, 0.120f, "crosshair" },         9 },
    };

    constexpr int ENTRY_COUNT = static_cast<int>(sizeof(ENTRIES) / sizeof(ENTRIES[0]));
}

int Game::GetOccupiedZones(Zone* zones, int maxZones)
{
    return GetOccupiedZones(zones, maxZones, nullptr, 0);
}

int Game::GetOccupiedZones(Zone* zones, int maxZones, const bool* hiddenElements, int hiddenCount)
{
    if (!zones || maxZones <= 0)
    {
        return 0;
    }

    int written = 0;

    for (int i = 0; i < ENTRY_COUNT && written < maxZones; ++i)
    {
        const Entry& entry = ENTRIES[i];

        // Something the player has switched off is not in the way any more.
        if (hiddenElements &&
            entry.elementIndex >= 0 &&
            entry.elementIndex < hiddenCount &&
            hiddenElements[entry.elementIndex])
        {
            continue;
        }

        zones[written++] = entry.zone;
    }

    return written;
}
