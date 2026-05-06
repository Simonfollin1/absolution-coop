# Speedrun Toolkit – Installation

## Alternativ A: Ladda ner färdig DLL (rekommenderat)

### Krav
- Hitman Absolution (Steam-versionen)
- Windows

### Steg

**1. Ladda ner HitmanAbsolutionSDK-loadern**

Gå till https://github.com/pavledev/HitmanAbsolutionSDK/releases och ladda ner den senaste releasen.
Du behöver dessa filer:
- `dinput8.dll`
- `dinput8_original.dll`

**2. Ladda ner SpeedrunToolkit.dll**

Gå till [Releases](../../releases) i detta repo och ladda ner `SpeedrunToolkit.dll` från den senaste releasen.

**3. Kopiera filerna till spelet**

Hitta Hitman Absolutions spelkatalog (högerklicka på spelet i Steam → Hantera → Bläddra bland lokala filer).

Kopiera dit:
```
HitmanAbsolution.exe          ← redan här
dinput8.dll                   ← från SDK-releasen
dinput8_original.dll          ← från SDK-releasen
mods/
  SpeedrunToolkit.dll         ← från detta repo
  mods.ini                    ← skapa denna fil
```

**4. Skapa mods.ini**

Skapa filen `mods/mods.ini` med följande innehåll:
```
SpeedrunToolkit
```

**5. Starta spelet**

Starta Hitman Absolution via Steam. När du är inne i spelet:
- Tryck **`~`** (tilde) för att öppna SDK-panelen
- Tryck **`L`** för att öppna/stänga Speedrun Toolkit-fönstret

---

## Alternativ B: Bygg från källkod

### Krav
- Visual Studio 2022 med C++-komponenten
- CMake 3.8+
- Git

### Steg

```bat
git clone https://github.com/pavledev/HitmanAbsolutionSDK.git
git clone <detta-repo> HitmanAbsolutionSDK/Mods/SpeedrunToolkit
```

Öppna `HitmanAbsolutionSDK/CMakeLists.txt` och lägg till `SpeedrunToolkit` i MODS-listan:
```cmake
set(MODS
    SpeedrunToolkit   # lägg till denna rad
    FreeCamera
    ...
)
```

Bygg:
```bat
cd HitmanAbsolutionSDK
cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build build --config Release --target SpeedrunToolkit
```

Den färdiga DLL:en hamnar i `build/.../Release/SpeedrunToolkit.dll`.
Följ sedan installationsstegen från Alternativ A.

---

## Använda verktyget

| Tangent | Funktion |
|---------|----------|
| `~` | Öppna/stäng SDK-panel |
| `L` | Öppna/stäng Speedrun Toolkit |
| `Numpad1–5` | Teleportera till bookmark-slot 1–5 |
| `F1` | Starta/stoppa segment-timer |
| `F2` | Nollställ segment-timer |

Alla tangenter kan konfigureras om i `mods/mods.ini`.

### Bookmarks och NPC:er

När du sparar en bookmark sparas både din position och alla NPC:ers positioner vid den tidpunkten. Vid teleport återställs spelaren och NPC:erna till sina sparade positioner. **NPC-beteende, alert-nivå och scripted events återställs inte** — det är en motorteknisk begränsning, inte en bugg.

### Timer

Timern pausar automatiskt under laddningsskärmar. Den mäter väggtid minus laddningstid — inte spelets interna IGT. Tillräckligt för att segmenttima routes och jämföra optimeringar.
