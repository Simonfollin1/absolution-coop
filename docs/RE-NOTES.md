# Reverse engineering notes — what to point Ghidra at, and why

Working notes for extending co-op past what the SDK already reaches. Nothing
here is speculative about *where* things are: every address below is one the
HitmanAbsolutionSDK already calls on the shipping Steam build, so they can be
imported as symbols before any analysis starts.

Target: **Hitman: Absolution 1.0.447.0, Steam, `HMA.exe`**, 32-bit, PE image
base `0x400000`. All RVAs are module-relative — add `0x400000` for the address
Ghidra shows by default.

Sanity check that the table matches your binary: `GetApplicationOptionBool` at
RVA `0x212930` is `0x612930` loaded, which is the address the Hitman-5-Server
contracts emulator independently patches. If that one lines up, the rest will.

---

## 0. What a first pass over the real binary established

Run 2026-08-16 with `docs/ghidra/HmaRecon.java` against Steam `HMA.exe`,
image base `0x400000`, language `x86:LE:32:default`.

**The address table is correct.** All thirty sampled addresses land on real
function starts. Everything below rests on that and it held.

**This binary carries RTTI, and the existing research says it does not.**
`RESEARCH.md` states the class layouts come from leaked PDBs "inte från
RTTI-dumpning", which was read as RTTI being absent. It is present: the image
is full of MSVC type descriptors — `.?AVZHM5LocomotionRootState@@`,
`.?AVZContract@@`, `.?AVZSharedSoundSensor@@`, `.?AVZHM5CameraProfileBlendDatabase@@`,
and so on.

Two consequences, and the second is the larger one:

- Vtable recovery is exact rather than inferred. Every MSVC vtable is preceded
  by a pointer to a `CompleteObjectLocator` naming its class, so the chain runs
  name string → type descriptor → locator → vtable. `docs/ghidra/HmaRtti.java`
  walks it.
- **Class identification no longer depends on the leak.** The PDBs give richer
  information — member names, types, offsets — but the class inventory and the
  vtable ownership are recoverable from the shipping binary alone.

**Walking a constructor for vtable stores does not work here.** The function at
`ZHitman5::ZHitman5` (`0x21B130`) decompiles to three lines:

```c
void __fastcall FUN_0061b130(int param_1)
{
  if (param_1 != 0) { FUN_0051d720(); return; }
}
```

A stub forwarding to `0x51D720`. The stores are further in. This is why the
RTTI route replaced it rather than supplementing it.

### Three levers found in the string sweep

None needed reverse engineering; all three were sitting in the strings.

**`HitmanDamageReceivedMultiplier.EASY|MEDIUM|HARD|EXPERT`** is an `HMA.ini`
`[Difficulty]` parameter: one line per difficulty, each holding 58
semicolon-separated values indexed by progression through the campaign. Setting
it to zero there makes the player take no damage with no memory write at all.

What it is **not** is a runtime console variable, which is what this document
claimed for a while and what the mod was written against. Enumerating every
variable the engine has registered, in the game, returned 1648 of them, and it
is not among them — filtering that list for "damage" gives `BloodBaseDamage`,
`CCChainFailDamage`, `CCCounterFailDamage`, `CCHitmanDamage`,
`CCSanchezChainFailDamage`, `CCSanchezCounterFailDamage`, `DebugDamageVal`,
`DebugWeaponDamage`, `OutfitDamageReductionPerRating` and `SBDamageMultiplier`,
and nothing else. So `ConfigVars::Set("HitmanDamageReceivedMultiplier", "0")`
dispatched into nothing and the mod's engine-level backstop did nothing at all
until this was noticed in-game.

The difficulty table is still the cleanest lever, but it is an ini edit, and
this mod does not edit anybody's `HMA.ini`. So the backstop is the SDK's own
player god-mode flag at `0xD4F5E0` — the one `Mods/Player` writes behind its
God Mode checkbox — set on arming and restored to whatever it held on the way
out. Steam only, since the address is the SDK's and the SDK's addresses are for
the build it was written against.

`0xD4D91C`, in `Mods/Actors`, is not a competing answer for the same flag: that
one is god mode for NPCs.

Beside it: `HealthRegenPerSecond.*`, `CriticalHealthThreshold.*`,
`HealthInterval0|1`, `NPCDamageReceivedMultiplier.*`, `CCHitmanDamage`.
The whole health model is configuration.

**`_root.g_mcHealthBar`** is the Scaleform health bar. `ZHUDManager::GetHUD()`
returns an `IScaleformPlayer*` and `GFxValue::GetMember` is at `0x19D5C0`, so
the player's real health is readable through the HUD — no offset, no hunting
for `ZHM5Health` in memory, and it cannot go stale against a patch.

**`OnTakeDamage`, `OnHitInfo`, `DamageTaken`, `OnGetHealth`, `LowHealth`,
`HealthReplenished`, `OnDrainHealthStarted`** read as entity *pin* names rather
than functions. Pin ids are CRC32 of the name, and `SignalInputPin` /
`SignalOutputPin` are at known addresses. Whether damage can be observed
through a pin instead of a vtable patch is untested, and would be the cleaner
mechanism if it works.

### The config system is bigger than anyone wrote down

The sweep is full of what look like registered console variables:

```
HealthRegenPerSecond.HARD          CriticalHealthThreshold.EXPERT
NPC_MaxCharacterStrafeBlendSpeed   LocoTest_StrafeRunSpeed
morpheme_DisableAnimation          ai_BehaviorTreeEvaluationsPerFrame
HM5DisguiseLODDissolveMin          debugchannel_drawnpclocomotion
DisguiseDetectionDistanceMultiplier.HARD    Disguise_MeterAttack
```

The SDK already exposes `ZConfigFloat`, `ZConfigInt` and `ZConfigCommand`, and
both `ZConfigCommand::First` (`0xD86D0`) and `ZConfigCommand::ExecuteCommand`
(`0x52EF10`) verified in section 1. `RESEARCH.md` lists "how far does the
console reach" as an open question. It is not open — a mod can walk the chain
at runtime and expose every engine variable in one panel, and that serves the
camera, detection and speedrun mods equally.

Fifteen functions call `ExecuteCommand`, and the string `"ConsoleCmd "` sits at
`0xE88754`.

### Morpheme node paths are literals in the binary

`ZHM5LocomotionNetwork::GetNodeID` takes a path string, and the paths are all
there:

```
RootNode|FullBody|Locomotion
RootNode|FullBody|Locomotion|LocomotionAim|Aim|CrouchAim
RootNode|UpperBodyOverride|Equip|EquipItem
EmotionStates|Combat|Locomotion|Move|Combat_Run
ControlledStates|HumanShield|Move|StrafeLeft
```

`LocomotionTransitEvents`, `strafe`, `UpperBodyEquip` and
`UpperBodyOverrideWeight` each carry 243 references, which is the shape of a
lookup table being filled in. That is the entry point for section 3.3.

Also confirming the provenance: a build path string reads
`d:\hitman5_delivery4\code\src\system\morpheme\...` — the same internal branch
name as the leaked 2012 development build.

### Not swept, worth adding

The string groups covered locomotion, morpheme, health, AI, checkpoints and
disguises. **Camera, rendering and post-processing were not swept**, which is
the gap for the CinematicCamera mod — particularly anything around
`ZRenderPostfilterParametersEntity` and the colour-correction palette whose
byte packing was never established. Add a group and re-run.

---

## 1. Import the symbols first

~110 named functions and globals are already known. Importing them turns a
stripped 34 MB binary into one where the interesting call graphs have names on
them, which is most of the work of getting started.

Save as `apply_hma_symbols.py` and run it from Ghidra's Script Manager:

```python
# Applies known HitmanAbsolutionSDK symbols to HMA.exe 1.0.447.0 (Steam).
from ghidra.program.model.symbol import SourceType

BASE = currentProgram.getImageBase().getOffset()   # 0x400000 normally

FUNCTIONS = {
    # --- hooked by the SDK core and its mods -------------------------------
    0x4FF520: "ZApplicationEngineWin32__MainWindowProc",
    0x5A7F30: "ZRenderDevice__Present",
    0x2FA520: "ZRenderSwapChain__Resize",
    0x55A620: "ZEngineAppCommon__Initialize",
    0x338F00: "ZEngineAppCommon__Uninitialize",
    0x53D390: "ZEngineAppCommon__ResetSceneCallback",
    0x58E8D0: "ZHitman5Module__Initialize",
    0x4479E0: "ZEntitySceneContext__CreateScene",
    0x265A80: "ZEntitySceneContext__ClearScene",
    0x565200: "ZEntityManager__ConstructUninitializedEntity",
    0x21B130: "ZHitman5__ctor",
    0x192990: "ZFreeCameraControlEntity__UpdateCamera",
    0x3DFA70: "ZFreeCameraControlEntity__UpdateMovementFromInput",
    0x1BC520: "ZCameraEntity__SetFovYDeg",
    0x46570:  "ZRenderPostfilterParametersEntity__UpdateParametersColorCorrection",
    0x1EF2C0: "ZKeyboardWindows__Update",
    0x3F1D90: "ZMouseWindows__Update",
    0x512940: "ZHeaderLibraryInstaller__Install",
    0x156280: "ZResourceLibraryLoader__AllocateEntry",
    0x1F150:  "ZResourceLibraryLoader__ProcessBlock",
    0x1E87B0: "ZResourceLibraryLoader__StartLoading",
    0xFFBF0:  "ZTemplateEntityBlueprintFactory__ctor",
    0x56BDB0: "ZHM5ReloadController__EndReloadWeapon",

    # --- entity system: the reflection layer -------------------------------
    0x82A20:  "SetPropertyValue",
    0x576C0:  "SignalInputPin",
    0x19BBA0: "SignalOutputPin",
    0x212930: "GetApplicationOptionBool",
    0xE253A8: "ZPropertyRegistry__GetPropertyName",

    # --- player, actors, world ---------------------------------------------
    0x174790: "ZActor__KillActor",
    0xE235A8: "ZHM5CCProfile__GetDefaultCCProfile",
    0x3B6390: "ZHitman5__EquipOutfitResource",
    0x5476A0: "ZHM5BaseInventory__AddItemToInventory",
    0x388C50: "ZCheckPointManagerEntity__ActivateJumpPoint",
    0x49A8C0: "ZGameLoopManager__SetPlayMode",
    0x8CC00:  "ZGameLoopManager__RegisterForFrameUpdate",
    0x59EFF0: "ZGameLoopManager__UnregisterForFrameUpdate",

    # --- spatial: how anything gets moved ----------------------------------
    0x2A7C40: "ZSpatialEntity__GetObjectToWorldMatrix",
    0x460D00: "ZSpatialEntity__SetObjectToWorldMatrix",
    0x379820: "ZSpatialEntity__GetWorldPosition",
    0x562060: "ZSpatialEntity__SetWorldPosition",

    # --- camera -------------------------------------------------------------
    0x3FA840: "ZEngineAppCommon__CreateFreeCameraAndControl",
    0x12A9E0: "ZFreeCameraControlEntity__SetActive",
    0x2E63B0: "ZFreeCameraControlEntity__GetUpdatedCameraRotation",
    0x431820: "ZFreeCameraControlEntity__GetUpdatedCameraPosition",
    0x5CACC0: "ZHM5MainCamera__SetCameraDirection",
    0x5645B0: "ZCameraEntity__SetNearZ",
    0xA0BF0:  "ZCameraEntity__SetFarZ",
    0x5BDD60: "ZCameraEntity__UpdateProjection",
    0x315BE0: "ZRenderManager__GetGameRenderDestinationEntity",
    0x36800:  "ZRenderManager__GetActiveRenderDestinationEntity",

    # --- input --------------------------------------------------------------
    0x2C5F50: "ZInputActionManager__AddBindings",
    0x48A4D0: "ZInputAction__Digital",
    0x32F930: "ZInputAction__Analog",
    0x7BDE0:  "ZInputAction__SetEnabled",

    # --- HUD / Scaleform ----------------------------------------------------
    0x1C5690: "ZHUDManager__ShowHUD",
    0x4BB670: "ZHUDManager__ShowThreatRadar",
    0x1F9670: "ZHUDManager__ShowTrespassingIcon",
    0x4CE8B0: "ZHUDManager__ShowAIStateIcon",
    0xED060:  "ZHUDManager__FadeHUDElements",
    0x490A00: "ZHUDManager__ScaleformShowWeaponDisplay",
    0x443840: "ZGameWideUIScaleformHandler__ShowUICursor",
    0x19D5C0: "GFxValue__GetMember",
    0x1E5720: "GFxValue__SetMember",

    # --- resources ----------------------------------------------------------
    0x48B520: "ZResourceManager__LoadResource",
    0x225BE0: "ZResourceManager__GetResourcePtr",
    0x6E5C0:  "ZResourceManager__GetResourceInstaller",
    0x34EFE0: "ZResourceManager__ReleaseStub",
    0x3DD4B0: "ZResourceManager__Update",
    0x31DFC0: "ZResourceLibraryInfo__InstallResource",
    0x25F990: "ZResourceDataBuffer__Create",
    0x228300: "ZResourcePtr__AddStatusChangedListener",
    0x5DBBE0: "ZResourcePtr__RemoveStatusChangedListener",
    0x40D4E0: "ZResourceLibrarySet__RemoveReadyCallback",
    0x5E1CD0: "ZResourceLibrarySet__Release",
    0x555C70: "ZDynamicResourceLibrary__ctor",
    0x30A450: "ZDynamicResourceLibrary__CreateEntities",
    0x612F80: "ZDynamicResourceLibrary__DeleteAllEntities",
    0x103500: "ZDynamicResourceLibrary__OnResourceLibrariesReady",
    0x34010:  "ZDynamicResourceLibrary__OnHeaderLibraryReady",
    0x354600: "LocalResourceIDsResolver__RecordMapping",

    # --- engine odds and ends ----------------------------------------------
    0x389F10: "SDK_GetMemoryManager",
    0x2EEFF0: "ZIniFile__LoadFromStringInternal",
    0x33BF40: "ZWin32ApplicationStub__ApplyOptionOverrides",
    0x52EF10: "ZConfigCommand__ExecuteCommand",
    0xD86D0:  "ZConfigCommand__First",
}

GLOBALS = {
    0xE31B80: "g_RenderManager",
    0xE21310: "g_LevelManager",
    0xD57190: "g_GraphicsSettingsManager",
    0xE24730: "g_GameTimeManager",
    0xE2EE10: "g_InputDeviceManager",
    0xE2F7D0: "g_InputActionManager",
    0xE21B30: "g_Hitman5Module",
    0xE24630: "g_GameLoopManager",
    0xE21690: "g_GameWideUI",
    0xD61C00: "g_HUDManager",
    0xE550F0: "g_ScaleformManager",
    0xD4DE98: "g_HM5InputControl",
    0xE54440: "g_CollisionManager",
    0xD47BFC: "g_pTypeRegistry",
    0xD58F30: "g_ContentKitManager",
    0xE258C0: "g_ResourceManager",
    0xDFDE70: "g_ActorManager",
    0xE25CCC: "g_pLocalResourceIDsResolver",
    0xE21580: "g_CheckPointManager",
    0xD64C30: "g_HM5ActionManager",
    0xE251A0: "g_EntityManager",
    0xCC6B90: "g_pApplicationEngineWin32",
    0xADC8EC: "vftable_ZTemplateEntityFactory",
    0xADC714: "vftable_ZTemplateEntityBlueprintFactory",
    0xADB874: "vftable_ZAspectEntityFactory",
    0xADB7EC: "vftable_ZAspectEntityBlueprintFactory",

    # Read by LiveSplit and by this repo. Useful as anchors: the code that
    # writes them is the code that changes level state.
    0xE53E20: "g_bIsInLoadingScreen",
    0xD61C7B: "g_bIsInMenu",
    0xE213E8: "g_bIsResultScreen",
    0xE20F48: "g_nCurrentLevel",
    0xD60F94: "g_nCurrentSection",
    0xE39520: "g_pTerminusElevator",
}

applied = 0

for rva, name in FUNCTIONS.items():
    address = toAddr(BASE + rva)
    if getFunctionAt(address) is None:
        createFunction(address, name)
    else:
        getFunctionAt(address).setName(name, SourceType.USER_DEFINED)
    applied += 1

for rva, name in GLOBALS.items():
    createLabel(toAddr(BASE + rva), name, True, SourceType.USER_DEFINED)
    applied += 1

print("applied %d symbols" % applied)
```

---

## 2. Answered: `YouGotHit` is slot 5

Settled 2026-08-16 with `docs/ghidra/HmaRtti.java`. Recovering
`ZHM5BaseCharacter`'s vtables through RTTI produced five tables, and every one
of them matches the entry count the SDK header declares:

| Sub-object offset | Base | Entries found | Header declares |
|---|---|---|---|
| `0x00` | `ZEntityImpl` | 24 | — |
| `0x08` | `IHM5BaseCharacter` | 5 | 5, adds nothing beyond `IComponentInterface` |
| `0x0c` | **`IBaseCharacter`** | **12** | **12** |
| `0x10` | `IMorphemeCutSequenceAnimatable` | 11 | 11 |
| `0x14` | `IBoneCollidable` | 3 | 3 |

Five independent agreements. The headers are faithful and the base order is
the declared one.

The `0x0c` table itself:

```
[ 0] 00a199f0                    destructor
[ 1] 004594d0                    GetVariantRef
[ 2] 0085e2e0   <-- same         AddRef
[ 3] 0085e2e0   <-- address      Release
[ 4] 00917400                    QueryInterface
[ 5] 0080df50   48 bytes         YouGotHit
[ 6] 00c758f0    5 bytes         CanProjectileHitCharacter
[ 7] 004f8b90                    GetCollisionLayer
[ 8] 0056b100  136 bytes         RegisterAttachment
[ 9] 005e03d0  279 bytes         UnregisterAttachment
[10] 0087fd40                    IsRagdoll
[11] 0087e6e0   37 bytes         GetLinkedEntityBase
[12] end of table
```

`0x0080df50` is `ZHM5BaseCharacter`'s own implementation. `ZHitman5` overrides
it at a different address, which is fine — the index is what the patch needs,
and an inherited vtable keeps its indices.

**`ZHitman5` has no type descriptor of its own.** Only
`TComponentInfo<ZHitman5, IEntity>` names it, so MSVC either never emitted one
or the linker folded it away. It does not matter here, but it is worth knowing
before going looking for one.

### The damage figure is still not settled

`YouGotHit`'s static call graph is nearly empty: one caller, `FUN_00717b40`,
and it is 45 bytes, so it forwards rather than computes. That is not a dead end
so much as the wrong tool. The function is reached virtually through the
vtable, and a static call graph cannot see an indirect call.

`FUN_00707230` (133 bytes), called from that same forwarder, is a plausible
candidate for a damage accessor, but plausible is not enough to build a health
model on. The mod charges a flat amount per hit and says so in its own UI.

If somebody wants to settle it: slot 5 is at byte offset `0x14` in the table,
so the call sites look like `call dword ptr [reg + 0x14]` against an
`IBaseCharacter`. Finding those is a search over code rather than over the
call graph.

### Locomotion: the network is `FUN_004041e0`

**A correction.** This section previously said `FUN_007b0d20` was
`ZHM5LocomotionNetwork::Init`, on the grounds that it was large and referenced
six node paths. Decompiling it shows that was wrong. It is a one-time
initialiser, guarded by `if (DAT_010397bc == -1)`, that registers every
morpheme node *name* and stores the resulting ids in globals. Size and string
count were not enough to identify it, and should not have been treated as if
they were.

It was still worth decompiling, because it gave three things:

- **`FUN_00882430` is the node-path lookup.** It takes a string and returns an
  id, which is what `ZHM5LocomotionNetwork::GetNodeID` needs.
- **The resolved ids live in static globals**, from `DAT_010397bc` upwards, one
  per named node. A mod can read them directly without calling anything.
- **`ZString`'s memory layout**, visible in the calls: `{0x8000000d,
  "EmotionStates"}` is thirteen characters with `0x80000000` set, so the shape
  is `{ size | 0x80000000, const char* }`. Useful for anything that has to
  build one by hand.

The network itself was found a different way: by asking who constructs the
locomotion states, which do carry RTTI even though the network does not.

| State | Constructed by | Size |
|---|---|---|
| `ZHM5LocomotionRootState` | `FUN_0062b0b0` | 18 |
| `ZHM5LocomotionMoveRootState` | `FUN_00421360` | 80 |
| `ZHM5LocomotionStandRootState` | `FUN_007099a0` | 43 |
| `ZHM5LocomotionStrafeRootState` | **`FUN_004041e0`** | 606 |
| `ZHM5LocomotionStrafeStandState` | **`FUN_004041e0`** | 606 |

The small ones are each state's own constructor. `FUN_004041e0` builds *two
different states*, and at 606 bytes has room for the rest, which is what an
owner constructing its members looks like. **`FUN_004041e0` is
`ZHM5LocomotionNetwork`'s constructor**, and the eighteen `ZHM5LocomotionState`
members laid out in the development build's header are what it is filling in.

From there: `SendRequest`, `SetControlParameter` and `Update` are methods on
the same object, so they are reachable by finding what else takes that `this`.

### Other vtables worth having

| Class | vtable | Constructed by |
|---|---|---|
| `ZCharacterController` | `00f26ddc`, `00e98d44` | `FUN_00b8a370` (720), `FUN_00b8a640` (118) |
| `ZMorphemeEntity` | `00ee9dc4`, `00ea41ec`, `00ea3a58` | `FUN_0062b340` (260), `FUN_007d6f10` (338) |
| `ZMonitorHitmanLocomotionStateEntity` | `00ec9bf4` | `FUN_00598b00`, `FUN_00678010` |
| `ZCompiledBehaviorTreeResourceInstaller` | `00ec5264` | `FUN_0090f7e0`, `FUN_004be1a0` |

`ZMonitorHitmanLocomotionStateEntity` is an ordinary entity: sixteen slots with
the `IComponentInterface` fingerprint at 2 and 3. If it can be found in a scene
it may be a cheaper way to read locomotion state than the network is.

### The damage figure: closed, not open

`FUN_00707230` is not a damage accessor. Decompiled it is an array grow and
insert: element size, begin/end/capacity pointers, growth by a quarter, a
placement-construction loop. It is a `std::vector` operation that happened to
be called nearby.

So the only remaining route to the real damage figure is finding the indirect
call sites, `call dword ptr [reg + 0x14]` against an `IBaseCharacter`. Until
somebody does that, the flat cost per hit stands and the UI says so.

### Console variables: the anchor did not work

Searching for functions referencing `ai_BehaviorTreeEvaluationsPerFrame`,
`morpheme_DisableAnimation` and three others returned nothing at all. The name
strings are almost certainly pushed as immediate operands that Ghidra did not
turn into data references. Enumerating the variables at runtime through
`ZConfigCommand::First`, which the mod already does, is the better route
anyway.

### The fingerprint this produced

`AddRef` and `Release` are separate declarations with the same trivial body, so
the linker folds them onto one address. **Every** `IComponentInterface`-derived
vtable in the dump shows slots 2 and 3 identical —
`ZHM5BaseCharacter`, `ZSharedSensorDef` and `ZCheckPointManagerEntity` all do,
across unrelated hierarchies — and `IMorphemeCutSequenceAnimatable`, which does
not derive from it, does not.

`DownedState::ValidateAndPatch` now checks that before patching. It is a real
test where the previous one was not: the old check asked whether the entries
looked like code, and every entry in a vtable looks like code, so it could
never have caught being off by one.

---

## 2b. The question this replaced

**Confirm which vtable slot `IBaseCharacter::YouGotHit` occupies.**

`coop/src/Game/DownedState.cpp` intercepts damage by replacing that entry in
the player's vtable, which is what lets co-op handle death without the engine
ever reloading a checkpoint. The slot is derived as **5** from the SDK's class
definitions: `IComponentInterface` declares five virtuals (destructor,
`GetVariantRef`, `AddRef`, `Release`, `QueryInterface`) and `IBaseCharacter`'s
own destructor overrides slot 0 rather than adding one.

Those headers are reconstructions. One virtual the header omits shifts
everything, and the code refuses to patch when its checks fail — so today the
feature is one confirmation away from being trustworthy instead of provisional.

How to answer it:

1. Open `ZHitman5__ctor` at RVA `0x21B130`.
2. A constructor writes one vtable pointer per base sub-object, as
   `mov dword ptr [reg + N], offset <vftable>`. List them. `ZHitman5` derives
   from `ZHM5BaseCharacter` (itself `ZEntityImpl`, `IHM5BaseCharacter`,
   `IBaseCharacter`, `IMorphemeCutSequenceAnimatable`, `IBoneCollidable`),
   plus `IFutureCameraState`, `ICharacterCollision`,
   `IHM5ActionEntityListener`, so expect roughly eight.
3. The `IBaseCharacter` one is identifiable by shape: entry 0 is a destructor,
   entries 1–4 are tiny (`GetVariantRef`, `AddRef`, `Release` and
   `QueryInterface` are all one-liners), and the next entry is substantial and
   takes a struct reference. That is `YouGotHit`.
4. Count its index. If it is 5, `kYouGotHitSlot` in `DownedState.cpp` is
   correct and the comment above it can lose its hedge. If not, change the
   constant.

Cross-check: `ZActor` derives from the same `ZHM5BaseCharacter`, so its
constructor writes an `IBaseCharacter` vtable with the same shape and a
different `YouGotHit`. Two independent sightings of the same index is a
confirmation; one is an observation.

---

## 2d. Confirmed in the game, on the retail build

One session with the logging on settled four things that had been assumptions.

**`ZHitman5` carries RTTI.** §2 assumed it did not and leaned on inherited
vtables instead. Walking the object at runtime and following each vtable's
`[-1]` complete object locator gives `.?AVZHitman5@@` for all five, with entry
counts that match the headers exactly:

```
+0x00  00AB790C  64 entries   ZEntityImpl + ZHM5BaseCharacter's own
+0x08  00A86938   5           IHM5BaseCharacter
+0x0C  00AA063C  12           IBaseCharacter      <- YouGotHit is slot 5
+0x10  00A965A0  11           IMorphemeCutSequenceAnimatable
+0x14  00A7B4D8   3           IBoneCollidable
```

Slot 5 of the `+0x0C` table read as an address inside `Coop.dll` in the dump,
which is the mod's own replacement entry looking back at us. That is the
interception confirmed from the outside.

**One caller.** All 256 hits came from `HMA.exe+00219A71`. `YouGotHit` is only
ever reached through a vtable, which is why §2's caller search found nothing;
the mod standing in the call can just read its own return address.

**The engine drops the dead from its alive-actor list.** 19 actor deaths in a
session: 19 seen by the actor leaving `ZActorManager`'s list, **zero** seen by
`IsDead()` becoming true on a listed actor. The list means what its name says.
Anything watching for a flag to flip will wait forever.

**Close combat is not in this path.** Nothing that happened in a close-combat
sequence produced a `YouGotHit` call at all, so a mod that intercepts damage
there intercepts nothing, and with god mode on the engine cannot apply it
either — the player is simply immune in close combat. The `CC*` names in the
configuration (`CCHitmanDamage`, `CCChainFailDamage`, `CCCounterFailDamage`)
say where that damage is decided, and it is somewhere else entirely.

---

## 2c. Input, and why a hotkey can look dead

Three separate things make a bound key do nothing, and the first two are not
bugs in the mod:

**The SDK owns the keyboard whenever its interface is up.**
`ImGuiRenderer::OnMainWindowProc` calls `InputActionManager->SetEnabled(!imguiHasFocus)`
on every message. So while any SDK panel has focus, every `ZInputAction::Digital()`
in every mod returns false. A key can only ever fire with the SDK closed —
which also means a mod's own panel is not being drawn at that moment, because
`OnDrawUI` is handed `imguiHasFocus` and every mod gates on it.

The consequence for a hotkey that opens a window: it must ask for focus at the
same time, or it sets a flag and shows nothing. `UI::RequestImGuiFocus` posts
the scan code left of `1` to the game window and lets the SDK's own handler do
the work. It is only ever safe to call from a hotkey, for the reason above —
if the hotkey fired, focus was elsewhere, so the post can only turn focus on.

**`ModInterface::AddBindings` compiles every action to `tap(kb,key)`.** A tap
is one frame of `Digital()` per press, which is right for a toggle and useless
for anything held — a spectator camera bound that way nudges once and stops.
The grammar has `hold(kb,key)`, but the SDK's generator never emits it, and
`GenerateBindingExpression` is protected and not exported, so a mod that wants
held keys has to build the block itself and hand it to
`ZInputActionManager::AddBindings` directly. That is what `CoopMod::InstallBindings`
does.

**`LoadConfiguration` only sets `modName` if the ini exists.** `AddBindings`
then builds the block as `Input={...}` with an empty name. Any mod that reaches
`AddBindings` without its ini installed is binding into a nameless block.

The accepted key tokens are the engine's, not Windows':
`f1`–`f15`, `a`–`z`, `0`–`9`, `left`, `right`, `up`, `down`, `pgup`, `pgdn`,
`home`, `end`, `prev`, `next`, `ins`, `del`, `space`, `tab`, `return`, `esc`,
`lctrl`/`rctrl`, `lshift`/`rshift`, `lalt`/`ralt`, `lwin`/`rwin`, `tilde`,
`grave`, `lbracket`, `rbracket`, `semicolon`, `apostrophe`, `slash`, `num0`–`num9`.

---

## 3. Targets ranked by what they unlock

### 3.1 `SHitInfo::GetBaseDamage` — real damage numbers

Today every intercepted hit costs a flat amount, because the magnitude lives
behind a method with no known address. Find it and co-op's health model stops
being an approximation.

`SHitInfo`'s layout is published for the development build:

```
ZEntityRef        m_HitEntity            +0x00
ZPhysicsObjectRef m_pHitBody             +0x04
unsigned int      m_nHitBoneIndex        +0x0C
IProjectile*      m_pProjectile          +0x10
float4            m_vHitPos              +0x20
float4            m_vHitNormal           +0x30
SExplodeInfo      m_Explosion            +0x40
unsigned int      m_nActorDeathType      ...
```

**And it is wrong for retail.** 256 hits captured whole in one session, all
from `HMA.exe+00219A71`, give this instead:

```
+0x00  ZEntityRef    m_HitEntity      constant across every hit - it is us
+0x04  (zero throughout)
+0x08  unsigned int  bone index       varies: 0x03, 0x04, 0x5D, 0x40...
+0x0C  IProjectile*  m_pProjectile    a different one every shot
+0x10  float4        m_vHitPos        w = 1.0, and a plausible world position
+0x20  float4        m_vHitNormal     w = 0.0, and unit length to 4 decimals
+0x30  onwards       byte-identical across all 256
```

Retail is the dev layout shifted back by `0x10`: `m_pHitBody` is four bytes
here, not eight. The mod had been reading `+0x20` as the hit position, which on
retail is the normal.

The tail from `+0x30` reads `600.0, 200.0, 6, 0.5, -100.0, 1.7, 6, 1.0, 1.0,
1.0, 5` and never moved, across four weapons and two dozen bone indices. Either
it is `SExplodeInfo` sitting at its defaults for non-explosive hits, or the
struct ends before it and that is the caller's stack frame — the address was
`0019E1D0` every single time, so a stable frame is entirely possible.

**Either way there is no damage figure in the struct.** `GetBaseDamage` reads
the projectile at `+0x0C`, and that is the only field that changes per shot in
a way that could carry one. The mod follows it now and captures `0x80` bytes
from it on every hit, which is where the number has to be if it is reachable at
all without disassembling `HMA.exe+00219A71`.

Anchor: whatever calls `YouGotHit` builds one of these. Work outwards from the
callers of the slot found in §2.

**The disassembler is the slow way round.** `YouGotHit` is only ever reached
through a vtable, so it has no direct callers for Ghidra to list — which is why
§2's caller search came back empty. The mod's replacement entry, on the other
hand, is standing in the call: `_ReturnAddress()` inside it *is* the caller, and
`SHitInfo` is right there by reference.

So the Research tab captures both. Every intercepted hit is copied whole and
kept, and the panel marks which dword offsets differed across the hits taken —
a slot reading 20 for a pistol and 60 for a shotgun is the damage figure, found
without reversing the accessor at all. The caller RVAs come out alongside it,
and those open directly at `base + rva`: that is the code which computed the
damage, which is the function §3.1 was asking for.

One session with a pistol, a shotgun and a fist settles it.

### 3.2 `ZHM5Health` — where the real hit points live

```cpp
struct ZHM5Health {
    int              m_nDamageHistoryNext;
    SDamageHistory   m_aDamageHistory[32];
    float            m_fDamageHistoryLength;
    TEntityRef<ZHitman5> m_pHitman;
    bool             m_bRefillHealth;
    bool             m_bDrainHealth;
    float            m_fHitPoints;
    float            m_fMaxHitPoints;
    ...
    static const float s_fDefaultMaxHitPoints;
};
```

Anchor: `s_fDefaultMaxHitPoints` is a static float, almost certainly `100.0`.
Search for the constant, find who reads it, and the struct falls out. Then find
where a `ZHM5Health` hangs off `ZHitman5` — the retail SDK's `ZHitman5` has
known members at `0xA30`, `0xA44`, `0xA58` and `0xC94` inside a `0xD20` object,
so the pointer is in one of the padded gaps between them.

Worth having for its own sake: a real health bar for a downed teammate is more
useful than a synthetic one.

**The in-game route, which is shorter.** The mod owns the vtable entry the
engine's damage handling goes through, so it can let exactly one hit past and
watch. The Research tab snapshots all `0xD20` bytes of `ZHitman5`, arms a
single pass-through, and diffs the two sides of the hit. Every dword that moved
is listed with its float reading, and a float that dropped by a plausible
amount is `m_fHitPoints` — or, if nothing in the object moved, the health lives
behind one of those padded pointers and the diff has narrowed it to the pointer
that was followed.

That hit is real and can kill, so it is behind its own button with its own
warning. It is also the only way to ask the question without a debugger
attached to a running game.

### 3.3 Locomotion — the animation driver, and the gate on visible avatars

This is what turns a floating marker into a character that walks.

From the development build's headers, neither of which has a retail address:

```cpp
void ZHM5LocomotionInput::Update(float dt, float rawInputX, float rawInputY,
                                 bool activateRun, bool sneak, float sneakWeight,
                                 bool strafe, bool allowFastWalk,
                                 const float4& camDir);

bool ZHM5LocomotionNetwork::SendRequest(ENetworkRequest request);
void ZHM5LocomotionNetwork::SetControlParameter(ENetworkControlParam, const ZVariantRef&);
```

`ENetworkRequest` is the movement vocabulary — `Move`, `TurnToMove`, `Stop`,
`Turn`, `StrafeForward`, `PlayIdleAnimation` and so on, eighteen in total. Feed
a spawned actor the stick values a remote player sent and the engine animates
it with its own blending, instead of sliding it along the ground.

("Network" here is Morpheme's animation network. It has nothing to do with
netcode, and the name collision in a co-op project is unfortunate.)

Anchor: `ZHitman5` inherits `ZHM5MorphemeNodeIds`, ~280 `unsigned int` node ids
initialised by name lookup. The strings behind them (`"LocomotionSM"`,
`"FullBodySM"`, and so on) are in the binary, and whatever reads them is inside
the locomotion setup.

### 3.4 Suppressing an actor's AI — the gate on a synchronised world

For the host to own an NPC and everyone else to puppet it, the puppets must
stop thinking. Two routes, neither confirmed:

- **Property route, needs no RE.** `m_pCompiledBehaviorTree` is a settable
  property; the SDK's Player mod already swaps it between guard and civilian
  trees. Whether it accepts null, and what the engine does about it, is an
  experiment somebody can run this afternoon.
- **Code route.** Find `ZBehaviorTreeEvaluator`'s per-frame tick and skip it for
  owned-elsewhere actors. Start from `runtime.crowds` and the AI behaviour-tree
  classes in the development build.

Try the property first. It costs an evening and might answer the whole thing.

---

## 4. Method, and its one rule

The development build (2012-12-18) ships complete private PDBs for every
module, and pavledev's `HitmanAbsolutionSDK2` is the result of mining them:
1590 headers with full class layouts and method signatures, published openly.
**Read SDK2's headers before disassembling anything** — the answer to "what is
this class shaped like" is usually already written down.

What SDK2 cannot give you is retail addresses. It targets the development
build, which is modular, so it links against DLL export tables and needs no
RVAs at all. That difference suggests the cheapest possible route for anything
hard:

> Prototype against the development build, where the whole API is free. Then
> port only the handful of functions the prototype actually used, by diffing
> those specific functions against retail.

The build is modular, version 1.0.0.0, and about five months older than retail,
so symbols do not map one to one. Expect anchoring on strings and vtables
rather than a clean BinDiff sweep.

**The rule:** keep the room clean. Derived addresses, offsets and names go in
this repo. Leaked binaries and symbol files do not, and neither does anything
copied verbatim out of them.
