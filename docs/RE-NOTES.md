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

## 2. The one question that would settle a shipped feature

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

Anchor: whatever calls `YouGotHit` builds one of these. Work outwards from the
callers of the slot found in §2.

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
