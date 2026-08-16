#include "Game/Announce.h"

#include <Windows.h>

#include <Logger.h>

namespace
{
    // Any name will do as long as all three mods agree on it.
    constexpr const char* CLAIM_VARIABLE = "FOLL1N_ABSOLUTION_BANNER";

    bool ClaimSignOff()
    {
        char existing[8] = {};

        if (GetEnvironmentVariableA(CLAIM_VARIABLE, existing, sizeof(existing)) > 0)
        {
            return false;
        }

        // A race here would print the line twice, which is a cosmetic problem
        // and not worth a synchronisation primitive: mods are initialised one
        // after another on the same thread.
        SetEnvironmentVariableA(CLAIM_VARIABLE, "1");

        return true;
    }
}

void Game::AnnounceReady(const char* line)
{
    if (!line || !line[0])
    {
        return;
    }

    Logger::GetInstance().Log(Logger::Level::Info, "{}", line);
}

void Game::AnnounceSignOffOnce()
{
    // Called from the draw path, so the common case has to be free: after the
    // first call this module never touches the environment again.
    static bool asked = false;

    if (asked)
    {
        return;
    }

    asked = true;

    if (!ClaimSignOff())
    {
        return;
    }

    Logger& log = Logger::GetInstance();

    log.Log(Logger::Level::Info, "");
    log.Log(Logger::Level::Info, "Everything above loaded. Press the key left of 1 for the SDK panel.");
    log.Log(Logger::Level::Info, "Have fun. - FOLL1N");
}
