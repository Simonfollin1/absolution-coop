# Absolution Co-op

Co-op for *Hitman: Absolution*.

Built on [pavledev's HitmanAbsolutionSDK](https://github.com/pavledev/HitmanAbsolutionSDK).

> **Heads up: only the single-player half has been played.** Damage
> interception, going down, the spectator camera and getting back up all work
> in the game. The networking has never been run against a second machine, so
> treat everything about hosting and joining as a claim rather than a fact.

## What you get

- See each other live in the same level, with nameplates and distance
- Reaching a checkpoint pulls everyone else forward to it
- Dying puts you on a spectator camera instead of reloading the level
- Shared event feed and chat
- Host and join by IP, up to 8 players

Everyone runs their own copy of the level, so **NPCs aren't shared**. Two
people in the same room see the same level and different guards. Doors,
bodies, disguises and alarms are local to each player too.

## Install

Grab the latest [release](../../releases), extract it into the folder
`HMA.exe` lives in, and start the game.

`~` opens the SDK panel, `F6` opens co-op. `F7` gets you up when you're down,
`F8` drops a marker, arrow keys move the camera while you're spectating.

Keys only work while the SDK's interface is closed, because it switches the
game's input off whenever it holds the keyboard.

Needs 1.0.447.0 and the 32-bit Visual C++ 2015-2022 redistributable. Don't use
it in Contracts.

## Playing together

One player hosts, and **only the host needs a reachable UDP port**. Everyone
else just types the host's `address:port`.

The host doesn't normally have to do anything about that port: pressing Host
asks the router to open it over UPnP, and the panel then shows the exact
address to hand to everyone else. It's closed again when you leave.

If the router says no — some have UPnP switched off — forward UDP 47474 by
hand, or put everyone on a VPN like ZeroTier or Radmin, which needs no router
configuration at all. There's no matchmaking and no relay server.

## When something's wrong

It writes down what happened, so a bug report doesn't need screenshots:

- `mods\Coop.log` — the timeline. Every hit and what was in it, every key going
  down, every checkpoint, every death, every time it armed or gave up.
- `coop-dump-NNN.txt`, next to `HMA.exe` — the facts. Which build you're on,
  what the keys compiled to, the player's vtables, the actor list, and all
  sixteen hundred engine config variables. Written by itself six seconds into
  the first level.

Both are plain text. Send them as they are.

## Docs

- [BUILDING.md](docs/BUILDING.md) for building from source
- [RE-NOTES.md](docs/RE-NOTES.md) for engine findings and reverse engineering

## Credits

Absolution modding is a small scene and this is standing on all of it.

- **[pavledev](https://github.com/pavledev)** wrote the SDK, the resource
  editor, the Blender pipeline and most of the open format documentation.
  Genuinely none of this exists without that work.
- **[SuiMachine](https://github.com/SuiMachine/LiveSplit.HitmanAbsolution)**
  for the memory offsets and the trick of telling Steam and GOG apart by
  image size.
- **[LennardF1989](https://github.com/LennardF1989/Hitman-5-Server)** and
  **[the Peacock Project](https://github.com/thepeacockproject/Cobra)** brought
  Contracts back after the servers went down. One address in there also
  happened to confirm this project's entire symbol table.
- The **[Glacier 2 modding Discord](https://discord.gg/6UDtuYhZP6)**, where
  most of what anyone knows about this engine got worked out.

## License

GPL-3.0, same as the `HitmanAbsolutionSDK` this is built on. Take it apart,
fork it, use whatever's useful. Full text in [LICENSE](LICENSE).
