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
namespace ModPresence
{
    // Names as they appear in mods/, without the extension.
    constexpr const char* CINEMATIC_CAMERA = "CinematicCamera";
    constexpr const char* NO_FILTER        = "NoFilter";
    constexpr const char* SPEEDRUN_TOOLKIT = "SpeedrunToolkit";

    // True when that mod's DLL is loaded. Cheap, but it takes the loader lock,
    // so this is for startup and the occasional re-check rather than per frame.
    bool IsLoaded(const char* modName);
}
