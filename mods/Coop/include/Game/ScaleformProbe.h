#pragma once

#include <string>
#include <vector>

namespace Coop::Game
{
    // What the HUD is actually made of, asked at runtime.
    //
    // The mod took the player's health away from the engine and drew its own
    // red vignette instead, on the belief that Absolution has no health meter.
    // It has one - the ring around the radar - and it is a Scaleform movieclip,
    // so it keeps showing full health while the mod's own pool empties. A
    // player watching the HUD is being told they are fine right up until they
    // fall over.
    //
    // Two ways to fix that, and this exists to find out which one is available:
    //
    //   1. Drive the clip directly. ZHUDManager::GetHUD returns an
    //      IScaleformPlayer with GetMember, and GFxValue has GetMember and
    //      SetMember, so if the ring's scale or frame is reachable by name it
    //      can be written every frame from the mod's pool.
    //   2. Stop blocking the damage and mirror the engine's own health instead,
    //      which makes the ring move by itself because the game is still the
    //      one moving it. Better in every way, and it needs an offset nobody
    //      has found yet.
    //
    // This settles (1) in one session, and (1) is worth having anyway as the
    // thing that draws a downed teammate's health.

    struct ScaleformMember
    {
        std::string path;
        std::string type;     // the GFxValue type name, or why it failed
        std::string value;    // for numbers, booleans and strings
        bool        found = false;
    };

    // Walks a fixed list of candidate names under _root and reports what is
    // there. Names come from the string sweep of the shipping binary, so they
    // are the game's own, not invented.
    std::vector<ScaleformMember> ProbeHud();

    // Everything ProbeHud found, one line each, into mods\Coop.log.
    void LogHudProbe();
}
