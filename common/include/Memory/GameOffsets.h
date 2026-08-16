#pragma once

#include <cstdint>
#include <Windows.h>

#include "Game/CheckpointAreas.h"

// Static memory offsets for game state the SDK does not expose.
//
// Source: LiveSplit.HitmanAbsolution by SuiMachine
// (github.com/SuiMachine/LiveSplit.HitmanAbsolution)
//
// The Steam and GOG builds of 1.0.447.0 have different layouts, so we detect
// which one we are running inside and pick the matching table. Detection uses
// the PE SizeOfImage, the same discriminator LiveSplit uses.
namespace GameOffsets
{
    enum class Build
    {
        Unknown,
        Steam,
        GOG
    };

    // Longest pointer chain we need (the finale explosion flag).
    constexpr int MAX_CHAIN_LENGTH = 8;

    struct PointerChain
    {
        uintptr_t base = 0;
        int       count = 0;                 // number of offsets applied after base
        uintptr_t offsets[MAX_CHAIN_LENGTH]{};
    };

    struct Table
    {
        uintptr_t isInLoadingScreen;
        uintptr_t isInMenu;
        uintptr_t isResultScreen;
        uintptr_t currentLevel;
        uintptr_t currentSection;
        uintptr_t terminusElevatorPtr;  // ptr -> +0x38 = bool
        PointerChain finaleExplosion;   // base 0 means unavailable on this build
    };

    // Base address of the game executable. The SDK resolves the game module as
    // the process' main module rather than by name - the executable is HMA.exe
    // on both Steam and GOG, so looking it up by any other name yields nullptr.
    uintptr_t GetModuleBase();

    Build       GetBuild();
    const char* GetBuildName();

    // False when the build is unrecognised. Every reader below then returns a
    // zeroed value rather than dereferencing an address we cannot trust.
    bool IsSupported();

    const Table& GetTable();

    // Drops everything the readers below remember about which memory is
    // mapped. Call it when the level changes: the pointers they walk lead into
    // objects a level owns, and the regions those live in are exactly what a
    // level change can hand back to the allocator.
    //
    // Cheap enough to call on every transition and harmless to call when
    // nothing changed - the next read pays one syscall to find out.
    void InvalidateProbeCache();

    // How many times the readers have asked the kernel about a page, and how
    // many times they answered from what they already knew. Both are totals
    // since the process started; the diagnostics tab turns them into a rate.
    //
    // The first number is the one that used to be eight to ten every frame.
    unsigned int GetProbeSyscalls();
    unsigned int GetProbeCacheHits();

    // Walks a pointer chain, bailing out on any null link. Returns false if the
    // chain is unavailable or broken this frame.
    bool ReadChainBool(const PointerChain& chain, bool& valueOut);

    // Reads a bool at pointerOffset -> +fieldOffset. Returns false when the
    // pointer is not usable.
    //
    // Both this and ReadChainBool follow pointers whose addresses come from a
    // community autosplitter rather than from anything we verified, so both
    // guard the dereference with a structured exception handler. A range check
    // cannot tell a stale pointer from a live one, and this runs every frame.
    bool ReadPointerBool(uintptr_t pointerOffset, uintptr_t fieldOffset, bool& valueOut);

    template<typename T>
    inline T Read(uintptr_t offset)
    {
        const uintptr_t base = GetModuleBase();

        if (!base || !offset || !IsSupported())
        {
            return T{};
        }

        return *reinterpret_cast<T*>(base + offset);
    }

    inline bool IsTerminusElevatorLoading()
    {
        bool value = false;

        return ReadPointerBool(GetTable().terminusElevatorPtr, 0x38, value) && value;
    }

    // True if the game is in any loading state. The Terminus elevator is a
    // scripted ride that loads without raising the normal loading flag, so it
    // needs its own check.
    inline bool IsLoading()
    {
        return Read<bool>(GetTable().isInLoadingScreen) || IsTerminusElevatorLoading();
    }

    inline bool IsInMenu()
    {
        return Read<bool>(GetTable().isInMenu);
    }

    inline bool IsResultScreen()
    {
        return Read<bool>(GetTable().isResultScreen);
    }

    inline bool IsFinaleExplosion()
    {
        bool value = false;

        return ReadChainBool(GetTable().finaleExplosion, value) && value;
    }

    inline int GetLevel()
    {
        return Read<int>(GetTable().currentLevel);
    }

    inline int GetSection()
    {
        return Read<int>(GetTable().currentSection);
    }

    // Snapshot of everything CheckpointAreas needs to name the current area.
    inline CheckpointAreas::State GetAreaState()
    {
        CheckpointAreas::State state;

        state.level            = GetLevel();
        state.section          = GetSection();
        state.resultScreen     = IsResultScreen();
        state.terminusElevator = IsTerminusElevatorLoading();
        state.finaleExplosion  = IsFinaleExplosion();

        return state;
    }
}
