#pragma once

// Human-readable names for Absolution's checkpoint areas.
//
// The game only exposes a level index (chapter, 0-25) and a section index. The
// mapping from those to actual named areas comes from SuiMachine's LiveSplit
// autosplitter (github.com/SuiMachine/LiveSplit.HitmanAbsolution), which the
// speedrun community already uses, so our area names line up with the ones
// runners talk about.
//
// Several areas share a level/section pair and are disambiguated by extra
// state: the result screen flag, the Terminus elevator loading flag, or the
// finale explosion flag. Chapters 5, 15, 16, 19, 20 and 23 have no level IDs of
// their own and therefore no entries here.
namespace CheckpointAreas
{
    // Extra conditions beyond level/section. "Any" means the flag is not part
    // of the match.
    enum class Flag
    {
        Any,
        Required,
        Forbidden
    };

    struct Area
    {
        int         level;
        int         section;
        Flag        resultScreen;
        Flag        terminusElevator;
        Flag        finaleExplosion;
        const char* id;         // e.g. "C2_TerminusBottom"
        const char* name;       // e.g. "Terminus - Bottom"
        int         chapter;
    };

    // Current game state used to resolve an area.
    struct State
    {
        int  level            = -1;
        int  section          = -1;
        bool resultScreen     = false;
        bool terminusElevator = false;
        bool finaleExplosion  = false;
    };

    // Returns nullptr when the state matches no known area.
    const Area* Resolve(const State& state);

    // Convenience: "Terminus - Bottom", or a "Level 3 / Section 2" fallback
    // written into fallbackBuffer when the area is unknown.
    const char* ResolveName(const State& state, char* fallbackBuffer, unsigned int bufferSize);

    // Full table, for UI listings.
    const Area* GetAll(unsigned int& countOut);
}
