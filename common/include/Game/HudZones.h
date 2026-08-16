#pragma once

namespace Game
{
    // A rectangle in screen fractions: 0,0 is the top left corner, 1,1 the
    // bottom right. Resolution-independent, which is the only way a layout
    // survives someone changing theirs.
    struct Zone
    {
        float x = 0.f;
        float y = 0.f;
        float width = 0.f;
        float height = 0.f;

        const char* name = "";
    };

    // Where the game's own HUD sits, so an overlay can be placed somewhere else.
    //
    // These are measured from the game rather than read from it, and that is a
    // deliberate limit rather than laziness. Reading a movieclip's _x and _y
    // would mean pulling a number out of GFxValue, whose public interface
    // offers GetBool and SetBool and nothing numeric - the union in the SDK's
    // header declares NValue as float where Scaleform's own is a double, and
    // nobody has established which is right on this build. Guessing at a memory
    // layout is exactly what the colour grade slider did, and it did not work.
    //
    // Approximate rectangles are enough for the job anyway. The question a
    // layout asks is "is the bottom left corner busy", not "is it busy to the
    // pixel".
    //
    // Returns how many were written, up to maxZones.
    int GetOccupiedZones(Zone* zones, int maxZones);

    // Same, minus anything the caller has hidden. Pass the indices of hidden
    // elements as reported by the HUD element switches; a hidden minimap frees
    // its corner, and the layout should know that.
    int GetOccupiedZones(Zone* zones, int maxZones, const bool* hiddenElements, int hiddenCount);
}
