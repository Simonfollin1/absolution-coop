#include "Memory/GameOffsets.h"

uintptr_t GameOffsets::GetModuleBase()
{
    // Never by name. The executable is HMA.exe, so asking for
    // "HitmanAbsolution.exe" returns null - and then every Read<T> below
    // dereferences a near-null address instead of reading the game.
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    return base;
}
