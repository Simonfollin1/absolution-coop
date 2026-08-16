#pragma once

// Which of our mods are loaded in this process.
//
// The mods overlap on purpose: No Filter does one thing that Cinematic Camera
// also does, so someone who wants the fix without the rest can have it. Loaded
// together they would fight - No Filter re-asserting every frame would make
// Cinematic Camera's toggle look broken - so they have to notice each other.
//
// The rule: Cinematic Camera is in charge when both are present. It has the
// same two switches plus partial strength, and it is the one with a UI to
// explain what is happening.
//
// Co-op overlaps differently. It takes the render destination over while a
// player is down, which is the same thing Cinematic Camera does when its free
// camera is on, and neither can win that - whichever writes last is what the
// player sees. There is no sensible automatic answer, so co-op says so and
// leaves the choice to whoever installed both.
namespace ModPresence
{
    // Names as they appear in mods/, without the extension.
    constexpr const char* CINEMATIC_CAMERA = "CinematicCamera";
    constexpr const char* NO_FILTER        = "NoFilter";
    constexpr const char* SPEEDRUN_TOOLKIT = "SpeedrunToolkit";
    constexpr const char* COOP             = "Coop";

    // True when that mod's DLL is loaded. Cheap, but it takes the loader lock,
    // so this is for startup and the occasional re-check rather than per frame.
    bool IsLoaded(const char* modName);
}
