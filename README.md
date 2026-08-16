# Absolution Co-op

Co-op for *Hitman: Absolution*.

There's also **Speedrun Toolkit** in here: a segment timer, position bookmarks
and a live position readout.

Both are DLL mods built on [pavledev's HitmanAbsolutionSDK](https://github.com/pavledev/HitmanAbsolutionSDK).

---

> ### Heads up: this is unproven
>
> It has never been run in the game, and until recently it had never been
> through a compiler either. Right now it's a design that's written down, not a
> mod that works. If you're willing to try it and tell me what broke, that's
> the fastest way that changes.

---

## How it works

Everyone runs their own copy of the level. You can see each other, you move
through it together, and the run is shared. The guards aren't.

| Shared | Yours alone |
|---|---|
| Where everyone is, about 20 times a second | NPCs and everything they do |
| Chapter and section | Bodies, disguises, alarms |
| Checkpoints, so one player opening the way ahead brings the others | Doors, pickups, items |
| Who's down | Scripted events and cutscenes |
| Event feed and chat | Score and rating |

So two people standing in the same room see the same level and different
guards. Syncing the AI would be a much bigger project, and might not be
possible at all.

## Moving through the level

When someone reaches a checkpoint, everyone else gets pulled to it after a
short grace window, long enough to finish whatever you were in the middle of.
Forward only. Nobody gets dragged backwards, because that would throw away
progress somebody made.

You can set it to pull automatically, ask first, or leave you alone.

## Dying

A checkpoint reload would wipe your world while everyone else carries on, so
the mod stops that from happening. Damage gets caught before it lands and taken
off a pool the mod keeps, and when that runs out you go to a spectator camera.
You're back in at the next checkpoint anyone reaches. Nothing reloads.

It catches damage with a vtable patch on the player object rather than a detour
at a fixed address, so it works on Steam and GOG both. If it can't verify what
it's patching it doesn't arm, and damage goes back to being handled normally.
The Diagnostics tab tells you which happened.

Two other settings if you'd rather: everyone restarts the section together, or
everyone carries on alone.

## Installing

1. Find the folder `HMA.exe` lives in.
   - **Steam:** right-click the game, Manage, Browse local files
   - **GOG:** `...\Hitman Absolution\retail\`
2. Extract the release archive straight into that folder, so `dinput8.dll` and
   `mods.ini` land next to `HMA.exe`.
3. Start the game.

`~` opens the SDK panel, `F6` opens co-op.

You'll need **1.0.447.0** and the 32-bit Visual C++ 2015-2022 redistributable.
Don't use this in Contracts.

## Hosting and joining

One player hosts. **Only the host needs a reachable UDP port**, since everyone
else connects out and the host passes packets between them.

- **Host:** pick a name and a port, hit Host. Forward that UDP port, or put
  everyone on the same LAN or VPN.
- **Join:** type the host's `address:port` and hit Join.

No matchmaking, no relay server. It's plain UDP over Winsock with no
dependencies, which keeps the build simple but does mean you get to deal with
your own router.

## Keys

| Key | |
|---|---|
| `~` | SDK panel |
| `F6` | Co-op panel |
| `F7` | Follow, when someone's opened the way ahead |

All rebindable in `mods/Coop.ini`.

## Building

The mod builds inside the SDK's tree.
[`.github/workflows/build.yml`](.github/workflows/build.yml) is the real
recipe, since it's what produces the releases, but the short version:

```bat
git clone --recurse-submodules https://github.com/pavledev/HitmanAbsolutionSDK.git sdk
git clone https://github.com/Simonfollin1/absolution-coop.git mod
xcopy /E /I mod\coop sdk\Mods\Coop
```

Add `Coop` to the `MODS` list in `sdk/CMakeLists.txt`, then:

```bat
cmake -S sdk -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=sdk/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build build --config Release --target DirectInputProxy HitmanAbsolutionSDK Coop
```

32-bit only.

One thing that will bite you if you skip it: build with
`/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. MSVC 14.40 made `std::mutex`'s
constructor `constexpr`, and without that flag the binary reads a null pointer
inside `std::mutex::lock` on any machine with an older Visual C++
redistributable. It crashes deep inside DirectXTK with nothing of yours on the
stack, so it's a miserable one to work out from scratch.

## Layout

```
coop/
  include/, src/
    Net/         UDP protocol, socket, session. Star topology, host relays.
    Game/        reads the engine, decides what to send and what to draw
    CoopMod      the ModInterface, the lobby, the overlay
docs/
  RE-NOTES.md    engine findings, addresses, and what's still unknown
  ghidra/        the scripts that produced them
```

The session runs on its own thread, so the game thread never waits on a socket.

If you want to extend any of this, read `docs/RE-NOTES.md` first. It has a
symbol table for the shipping binary, a Ghidra script that applies it, and a
straight account of which findings are confirmed and which are still guesswork.

## Credits

- **[pavledev](https://github.com/pavledev)** for the SDK, the resource editor,
  and most of the open format documentation this scene runs on. None of this
  exists without it.
- **[SuiMachine](https://github.com/SuiMachine/LiveSplit.HitmanAbsolution)**
  for the memory offsets for chapter, section and loading state, plus the trick
  of telling Steam and GOG apart by image size.
- **[LennardF1989](https://github.com/LennardF1989/Hitman-5-Server)** and
  **[the Peacock Project](https://github.com/thepeacockproject/Cobra)** for the
  Contracts server emulators. One address in there happened to confirm this
  project's entire symbol table.

## License

GPL-3.0, and not by choice. This links against `HitmanAbsolutionSDK`, which is
GPL-3.0, so anything built from it is too. Full text in [LICENSE](LICENSE).
