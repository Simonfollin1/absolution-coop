#include "Game/ModPresence.h"

#include <cstdio>

#include <Windows.h>

bool ModPresence::IsLoaded(const char* modName)
{
    if (!modName)
    {
        return false;
    }

    char moduleName[128];
    std::snprintf(moduleName, sizeof(moduleName), "%s.dll", modName);

    // GetModuleHandle does not take a reference, so there is nothing to release
    // and no chance of keeping a mod alive by asking about it.
    return GetModuleHandleA(moduleName) != nullptr;
}
