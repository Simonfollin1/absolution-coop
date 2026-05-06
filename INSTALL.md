# Speedrun Toolkit – Installation

## Ladda ner och spela (rekommenderat)

### Krav
- Hitman Absolution (Steam-versionen, PC)
- Windows

### Steg

**1. Ladda ner `SpeedrunToolkit.zip`**

Gå till [Releases](../../releases) och ladda ner `SpeedrunToolkit.zip` från den senaste releasen.

**2. Hitta spelkatalogen**

Öppna Steam → högerklicka på Hitman Absolution → Hantera → Bläddra bland lokala filer.

**3. Extrahera zip-filen till spelkatalogen**

Extrahera **innehållet** i zip-filen direkt till spelkatalogen (inte zip-filen i sig).
Efter extraktion ska det se ut så här:

```
HitmanAbsolution.exe          ← redan här
dinput8.dll                   ← från zip-filen
dinput8_original.dll          ← från zip-filen
mods/
  SpeedrunToolkit.dll         ← från zip-filen
  mods.ini                    ← från zip-filen
```

**4. Starta spelet**

Starta Hitman Absolution via Steam. Tryck **`~`** (tilde) för att öppna SDK-panelen, sedan **`L`** för att öppna Speedrun Toolkit.

---

## Använda verktyget

| Tangent | Funktion |
|---------|----------|
| `~` | Öppna/stäng SDK-panel |
| `L` | Öppna/stäng Speedrun Toolkit |
| `F1` | Starta/stoppa segment-timer |
| `F2` | Nollställ segment-timer |
| `Numpad1–5` | Teleportera till bookmark-slot 1–5 |

Alla tangenter kan konfigureras i `mods/mods.ini`.

### Bookmarks och NPC-positioner

Varje bookmark sparar din position och alla synliga NPC:ers positioner vid det tillfället.
Vid teleport återställs du och NPC:erna till de sparade positionerna.

**NPC-beteende, alert-nivå och scripted events återställs inte** — det är en motorteknisk begränsning i Glacier 2, inte en bugg. Tänk på bookmarks som positions-reset, inte kompletta savestates.

### Overlay-timern

Timern pausar automatiskt under laddningsskärmar. Den mäter väggtid minus laddningstid — tillräckligt precist för att segmenttima routes och jämföra optimeringar mellan försök.

---

## Bygg från källkod

### Krav
- Visual Studio 2022 med C++-komponenten
- CMake 3.8+
- Git

```bat
git clone https://github.com/pavledev/HitmanAbsolutionSDK.git
git clone <detta-repo> HitmanAbsolutionSDK/Mods/SpeedrunToolkit
```

Lägg till `SpeedrunToolkit` i MODS-listan i `HitmanAbsolutionSDK/CMakeLists.txt`:
```cmake
set(MODS
    SpeedrunToolkit
    FreeCamera
    ...
)
```

```bat
cd HitmanAbsolutionSDK
git clone https://github.com/microsoft/vcpkg.git
vcpkg\bootstrap-vcpkg.bat

cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows

cmake --build build --config Release --target DirectInputProxy SpeedrunToolkit
```

Kopiera `dinput8.dll`, `SpeedrunToolkit.dll` och `mods.ini` till spelkatalogen enligt stegen ovan.
