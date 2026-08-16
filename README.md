# Absolution Co-op

Co-op for *Hitman: Absolution* — a game that shipped without it, on an engine
that was never asked to allow it.

Also in this repository: **Speedrun Toolkit**, a segment timer, position
bookmarks and a live position readout.

Both are DLL mods built on [pavledev's HitmanAbsolutionSDK](https://github.com/pavledev/HitmanAbsolutionSDK).

---

> ### Status: unproven
>
> This has never been run in the game, and until recently it had never been
> through a compiler. Treat it as a design that is written down rather than a
> mod that works. Bug reports from anyone willing to try it are the fastest
> way that changes.

---

## What co-op means here

Absolution holds exactly one player. `ZLevelManager` owns a single `ZHitman5`,
one camera, one HUD, and mission logic that assumes one Agent 47. The AI is not
deterministic either — behaviour trees, a crowd system and PhysX, none of which
replay the same way twice.

So two machines cannot be made to simulate one world. What they can do is run
the same level side by side and agree about the parts that matter. That is the
model every co-op mod for a single-player game uses, Skyrim Together and Elden
Ring Seamless included, and it is the only one this engine allows.

**Everyone runs their own copy of the level.** You see each other, you move
through it together, and the run is shared. The guards are not.

| Shared between players | Local to each player |
|---|---|
| Where everyone is, ~20 times a second | NPCs, and everything they do |
| Chapter and section | Bodies, disguises, alarms |
| Checkpoints — one player opening the way ahead brings the others | Doors, pickups, items |
| Who is down | Scripted events and cutscenes |
| A shared event feed and chat | Score and rating |

Two people standing in the same room see the same level and different guards.
That is a consequence of the engine, not a bug, and no amount of work on this
mod will change it without synchronising the AI as well.

## Moving through the level together

Reach a checkpoint and everyone else is pulled to it, after a grace window long
enough to finish whatever you were in the middle of. Forward only — nobody is
ever dragged backwards, because that would delete progress somebody made.

Three settings: pull automatically, ask first, or never move me.

## Dying

Absolution answers a death by reloading a checkpoint. In a session where
everyone runs their own copy of the level, that reload wipes one player's whole
world while the others carry on — the single most destructive thing that can
happen to a shared run.

So the engine is never told. Damage is intercepted before it lands and scored
against a pool this mod owns, and running that pool out puts you on a spectator
camera instead of a loading screen. You come back at the next checkpoint anyone
reaches. The level is untouched and the run continues.

The interception is a vtable patch on the player instance rather than a detour
at a fixed address, which is why it works on Steam and GOG alike. If it cannot
verify what it is patching it refuses to arm, and the engine keeps handling
damage itself — see the Diagnostics tab.

Two other policies are available if you would rather have them: everyone
restarts the section together, or everyone carries on alone.

## Installing

1. Find the folder `HMA.exe` lives in.
   - **Steam:** right-click the game → Manage → Browse local files
   - **GOG:** `...\Hitman Absolution\retail\`
2. Extract the contents of the release archive straight into that folder, so
   `dinput8.dll` and `mods.ini` end up next to `HMA.exe`.
3. Start the game.

`~` opens the SDK panel. `F6` opens co-op.

Requires **Hitman: Absolution 1.0.447.0** and the 32-bit Visual C++ 2015–2022
redistributable. Do not use it in Contracts.

## Hosting and joining

One player hosts. **Only the host needs a reachable UDP port** — everyone else
connects out, and the host relays between them.

- **Host:** set a name and a port, press Host. Forward that UDP port, or put
  everyone on the same LAN or VPN.
- **Join:** type the host's `address:port` and press Join.

There is no matchmaking and no relay service. The transport is plain UDP over
Winsock with no dependencies, which is a deliberate trade: it keeps the build
simple at the cost of making you deal with your own router.

## Keys

| Key | |
|---|---|
| `~` | SDK panel |
| `F6` | Co-op panel |
| `F7` | Follow, when somebody has opened the way ahead |

All rebindable in `mods/Coop.ini`.

## Building

The mod builds as part of the SDK's tree. Full details are in
[`.github/workflows/build.yml`](.github/workflows/build.yml), which is the
authoritative recipe — it is what produces the releases.

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

32-bit is not optional — the game is a 2012 x86 binary and always will be.

Build with `/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. MSVC 14.40 made
`std::mutex`'s constructor `constexpr`, and a binary built without that flag
reads a null pointer inside `std::mutex::lock` on any machine carrying an older
Visual C++ redistributable. The failure lands deep inside DirectXTK with
nothing of yours on the stack, so it is not a thing to rediscover.

## How it works

```
coop/
  include/, src/
    Net/         UDP protocol, socket, session — star topology, host relays
    Game/        reads the engine, decides what to send and what to draw
    CoopMod      the ModInterface, the lobby, the overlay
docs/
  RE-NOTES.md    engine findings, addresses, and what is still unknown
  ghidra/        scripts that produced them
```

The session owns one worker thread. The game thread never waits on a socket —
a stalled frame in a 32-bit DirectX 11 title is not a dropped packet, it is a
hitch the player sees.

`docs/RE-NOTES.md` is worth reading if you want to extend this. It carries a
symbol table for the shipping binary, a Ghidra script that applies it, and an
honest account of which findings are confirmed and which are still inference.

## Credits

- **[pavledev](https://github.com/pavledev)** — the SDK, the resource editor,
  and most of the open format documentation this scene runs on. None of this
  exists without it.
- **[SuiMachine](https://github.com/SuiMachine/LiveSplit.HitmanAbsolution)** —
  the memory offsets for chapter, section and loading state, and the trick of
  telling Steam from GOG by image size.
- **[LennardF1989](https://github.com/LennardF1989/Hitman-5-Server)** and
  **[the Peacock Project](https://github.com/thepeacockproject/Cobra)** — the
  Contracts server emulators, and one address that independently corroborated
  this project's whole symbol table.

## License

GPL-3.0. Not a choice: this links against `HitmanAbsolutionSDK`, which is
GPL-3.0, so anything built from it is too. Full text in [LICENSE](LICENSE).
