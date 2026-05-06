#include "Memory/GameOffsets.h"

uintptr_t GameOffsets::GetModuleBase()
{
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA("HitmanAbsolution.exe"));
    return base;
}
