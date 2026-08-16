# Building

[`.github/workflows/build.yml`](../.github/workflows/build.yml) is the real
recipe, since it's what produces the releases. This is the short version for
building locally.

## Setup

```bat
git clone --recurse-submodules https://github.com/pavledev/HitmanAbsolutionSDK.git sdk
git clone https://github.com/Simonfollin1/absolution-coop.git mod
xcopy /E /I mod\coop sdk\Mods\Coop
```

Add `Coop` to the `MODS` list in `sdk/CMakeLists.txt`.

```bat
cmake -S sdk -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=sdk/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build build --config Release --target DirectInputProxy HitmanAbsolutionSDK Coop
```

32-bit only. The game is a 2012 x86 binary.

## Things that will bite you

**Build with `/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`.** MSVC 14.40 made
`std::mutex`'s constructor `constexpr`, and without that flag the binary reads
a null pointer inside `std::mutex::lock` on any machine carrying an older
Visual C++ redistributable. It crashes deep inside DirectXTK with nothing of
yours on the stack, which is a miserable thing to work out from scratch. Pass
it through the `CL` environment variable, not a CMake flag, or it won't reach
vcpkg's own build of DirectXTK.

**`mods.ini` goes in the game root, not `mods/`.** `ModManager::LoadAllMods`
reads it from the process working directory, and it treats each ini *section*
name as a mod name. So `[Coop]`, not `Coop`, and not in the mods folder.

**The SDK needs `--recurse-submodules`.** A plain clone leaves ImGui empty.

**`Logger.cpp` is missing `#include <chrono>`** on MSVC 19.44 and up. CI
patches it before building.

**One owner per hook address.** MinHook ships as a shared DLL, so two mods
hooking the same address means the second one silently fails. Nothing here
installs a detour, but that's worth knowing before you add one.

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

Read [RE-NOTES.md](RE-NOTES.md) before extending any of this. It has a symbol
table for the shipping binary, a Ghidra script that applies it, and a straight
account of which findings are confirmed and which are still guesswork.
