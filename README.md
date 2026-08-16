# Absolution Co-op

Co-op for *Hitman: Absolution*.

Built on [pavledev's HitmanAbsolutionSDK](https://github.com/pavledev/HitmanAbsolutionSDK).

> **Heads up: this is unproven.** It has never been run in the game. Right now
> it's a design that's written down, not a mod that works. If you try it and
> tell me what broke, that's the fastest way that changes.

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

`~` opens the SDK panel, `F6` opens co-op.

Needs 1.0.447.0 and the 32-bit Visual C++ 2015-2022 redistributable. Don't use
it in Contracts.

## Playing together

One player hosts, and **only the host needs a reachable UDP port**. Forward it,
or put everyone on the same LAN or VPN. Everyone else just types the host's
`address:port`.

There's no matchmaking or relay server, so you get to deal with your own
router.

## Docs

- [BUILDING.md](docs/BUILDING.md) for building from source
- [RE-NOTES.md](docs/RE-NOTES.md) for engine findings and reverse engineering

## Credits

- **[pavledev](https://github.com/pavledev)** for the SDK, the resource editor,
  and most of the open format documentation this scene runs on
- **[SuiMachine](https://github.com/SuiMachine/LiveSplit.HitmanAbsolution)**
  for the memory offsets and the Steam/GOG detection trick
- **[LennardF1989](https://github.com/LennardF1989/Hitman-5-Server)** and
  **[the Peacock Project](https://github.com/thepeacockproject/Cobra)** for the
  Contracts server emulators

## License

GPL-3.0, and not by choice. This links against `HitmanAbsolutionSDK`, which is
GPL-3.0, so anything built from it is too.
