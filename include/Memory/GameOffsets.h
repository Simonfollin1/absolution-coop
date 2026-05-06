#pragma once

#include <cstdint>
#include <Windows.h>

// Steam version memory offsets.
// Source: LiveSplit.HitmanAbsolution by SuiMachine (github.com/SuiMachine/LiveSplit.HitmanAbsolution)
// All offsets are relative to HitmanAbsolution.exe module base.
namespace GameOffsets
{
    constexpr uintptr_t IS_IN_LOADING_SCREEN  = 0xE53E20; // bool
    constexpr uintptr_t IS_IN_MENU            = 0xD61C7B; // bool
    constexpr uintptr_t IS_RESULT_SCREEN      = 0xE213E8; // bool
    constexpr uintptr_t CURRENT_LEVEL         = 0xE20F48; // int, chapter 0-25
    constexpr uintptr_t CURRENT_SECTION       = 0xD60F94; // int, section/area ID
    constexpr uintptr_t TERMINUS_ELEVATOR_PTR = 0xE39520; // ptr -> +0x38 = bool

    uintptr_t GetModuleBase();

    template<typename T>
    inline T Read(uintptr_t offset)
    {
        uintptr_t addr = GetModuleBase() + offset;
        if (!addr) return T{};
        return *reinterpret_cast<T*>(addr);
    }

    // Returns true if the game is currently in any loading state.
    inline bool IsLoading()
    {
        if (Read<bool>(IS_IN_LOADING_SCREEN)) return true;

        uintptr_t elevPtr = Read<uintptr_t>(TERMINUS_ELEVATOR_PTR);
        if (elevPtr && *reinterpret_cast<bool*>(elevPtr + 0x38)) return true;

        return false;
    }
}
