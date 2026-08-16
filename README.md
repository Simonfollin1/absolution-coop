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

There's no matchmaking or relay server, so you'll need to sort out port
forwarding yourself. A VPN like ZeroTier or Radmin is the easy way around it.

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
