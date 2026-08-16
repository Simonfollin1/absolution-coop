#pragma once

namespace Game
{
    // Writes to the SDK's console window - the one every player sees on launch.
    //
    // Worth using sparingly and worth using at all. That window currently ends
    // mid-sentence in the SDK's own internals, so the last thing anyone reads
    // before the game starts is a wall of hook registrations. One clear line per
    // mod turns it into an answer to the only question a player actually has:
    // did it work, and what do I press.
    void AnnounceReady(const char* line);

    // The closing line, printed exactly once no matter how many of these mods
    // are installed.
    //
    // Each mod is its own DLL with its own globals, so a static flag cannot be
    // shared between them. A process environment variable can: it is per
    // process, visible to every module in it, and costs nothing. Whichever mod
    // gets there first claims the line; the others stay quiet.
    //
    // Call it from the first UI draw rather than from initialisation, so it
    // lands after every mod has had its say rather than in the middle of them.
    void AnnounceSignOffOnce();
}
