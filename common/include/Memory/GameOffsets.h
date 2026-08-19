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

    // Copies `size` bytes from base + offset, but only after probing that the
    // page is committed and readable, and inside a structured handler. Returns
    // false and leaves `out` untouched on any failure.
    bool ReadBytes(uintptr_t offset, void* out, size_t size);

    // A scalar global, read the same guarded way as everything else here. The
    // meanings of these offsets are autosplitter-sourced and unverified against
    // the binary, so a wrong one has to fail soft, not fault - which a bare
    // dereference would not. Returns T{} when the read cannot be made.
    template<typename T>
    inline T Read(uintptr_t offset)
    {
        T value{};

        return ReadBytes(offset, &value, sizeof(T)) ? value : T{};
    }

    // The least-supported offset in this file. It is autosplitter-sourced, and
    // reading HMA.exe found the pointer at terminusElevatorPtr referenced only
    // by sound code, never written where the static image can see it and never
    // obviously an elevator-loading object - so the +0x38 loading bool rests on
    // runtime state the binary does not corroborate. It is guarded like
    // everything else and fails soft, so a wrong reading costs at worst a
    // held-too-long or dropped-too-early loading check on the Terminus level,
    // not a crash. Confirm it in a live Terminus session (log *(ptr) and the
    // +0x38 byte across the elevator ride) before trusting it.
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
