#include "Game/MinHookSetup.h"
#include "Diag/Diag.h"

// Via the SDK's header rather than <MinHook.h> directly - that is the include
// path a mod is known to be able to reach.
#include <Hooks.h>

bool Game::EnsureMinHook()
{
    const MH_STATUS status = MH_Initialize();

    if (status == MH_OK)
    {
        Diag::Log("MinHook initialised by this mod");
        return true;
    }

    if (status == MH_ERROR_ALREADY_INITIALIZED)
    {
        // The normal case: the SDK got there first.
        return true;
    }

    Diag::Log("MinHook unavailable (%d) - any hook installed later will fail",
        static_cast<int>(status));

    return false;
}
