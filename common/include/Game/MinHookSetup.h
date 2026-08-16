#pragma once

namespace Game
{
    // Brings MinHook up, or confirms it is already up.
    //
    // Replaces calling ModInterface::Initialize(), whose entire body is an
    // MH_Initialize() call that logs "Failed to initialize MinHook!" at ERROR
    // on anything other than MH_OK. By the time a mod loads, the SDK has always
    // initialised MinHook already, so every mod prints that line on every
    // launch - three ERRORs in the console before the game has even started,
    // none of which mean anything.
    //
    // A log that cries wolf at every startup is worse than no log: the first
    // real error scrolls past unnoticed among the fake ones. This treats
    // MH_ERROR_ALREADY_INITIALIZED as what it is - success - and says nothing.
    //
    // Returns false only when MinHook genuinely is not available, which is
    // worth knowing: hooks installed later will fail too.
    bool EnsureMinHook();
}
