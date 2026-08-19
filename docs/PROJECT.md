# The project, in full

What this is, how it works, what has actually been proven in the game, and what
went wrong on the way. Written for whoever picks it up next, including us.

`RE-NOTES.md` is the engine reference — addresses, layouts, how each was
established. This is the account of the work.

---

## 1. The decision everything else follows from

Absolution has no multiplayer, no dedicated server binary, and no deterministic
simulation. `ZLevelManager` holds exactly one `ZHitman5`, and the AI is driven
by behaviour trees over a live physics world. Two machines cannot be made to
simulate the same world and stay in agreement.

So they don't. **Every player runs their own copy of the level.** The mod
replicates the players to each other and nothing else. Two people standing in
the same room see the same level and different guards.

This is the same shape Skyrim Together and Elden Ring Seamless take, for the
same reason, and it decides everything downstream:

- Positions, orientation and state are replicated. NPCs, doors, bodies,
  disguises and alarms are not.
- You cannot blow each other's cover. One player's firefight leaves the other's
  level quiet.
- Progress is shared as *events* — "I reached a checkpoint", "I am in this
  level" — not as world state.
- Anything the engine does in response to a death is a catastrophe, because it
  resets one player's whole world while the others carry on. Which is why the
  largest single system in the mod exists to stop the engine ever learning that
  a player died.

---

## 2. How it is built

Built against [pavledev's HitmanAbsolutionSDK](https://github.com/pavledev/HitmanAbsolutionSDK),
32-bit, MSVC, through GitHub Actions on `windows-2022`. The workflow clones the
SDK fresh each time, drops `mods/Coop` into its `Mods` tree with `common/`
copied alongside, patches the SDK, builds, and packages.

**The SDK patches**, all in `.github/scripts/`:

- `patch-sdk-startup.ps1` — three defects around the proxy loader, the important
  one being `LoadLibrary` called from `DllMain`. That is forbidden: `DllMain`
  holds the loader lock and `LoadLibrary` wants it. Most of the time Windows
  untangles it; the rest of the time the game never finishes starting. It is why
  people were clicking Play four times.
- `patch-sdk-appearance.ps1` — removes the SDK's own branding from the menu bar
  and restyles the magenta to neutral greys.
- Two inline patches: a missing `<chrono>` include that MSVC 19.44 needs, and
  downgrading the "HashMap.txt missing" log line from ERROR to INFO, since we
  ship without that 28 MB file deliberately.

**Packaging**, which was wrong three separate ways before it worked:

- `mods.ini` goes in the game root, not `mods/` — `ModManager::LoadAllMods`
  reads it from the process working directory.
- It is an ini with `[Coop]` as a *section*, not a list of names. `LoadAllMods`
  walks sections.
- `HitmanAbsolutionSDK.dll` has to ship. The proxy exists to load it, and it was
  not in the archive at all.

`CL=/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` is set for the whole build. MSVC
14.40 made `std::mutex`'s constructor constexpr, and a binary built with it
reads a null pointer inside `std::mutex::lock` against an older Visual C++
redistributable. The failure lands deep in somebody else's code with nothing of
ours on the stack.

---

## 3. What the mod does

### 3.1 Damage interception and going down

The core system, and the one that made co-op possible at all.

`IBaseCharacter::YouGotHit` is **slot 5** of a 12-entry vtable. The mod patches
that slot on the player instance and **never chains to the original**. Not
calling it is the mechanism: the engine is not told, so it cannot start a death
sequence, so nobody's world reloads.

A vtable write on one instance rather than a detour at a fixed address, which is
what makes it work on Steam and GOG alike and why it needs no reverse
engineering to place.

Validation before patching: the vtable must be inside `HMA.exe`, entries up to
the target must be executable, and **slots 2 and 3 must be identical** —
`IComponentInterface`'s `AddRef` and `Release` are separate declarations with
the same trivial body, so the linker folds them onto one address. Every
derived vtable in the RTTI dump shows this and the one interface that does not
derive from it does not. That is a fingerprint, where "these look like code"
was not: every entry in a vtable looks like code.

Damage is scored against a pool the mod owns. Running it out puts the player on
a spectator camera. Coming back is a self-revive timer, a teammate reaching a
checkpoint, or a scene change, followed by a short window of immunity so you are
not dropped again by the guard you stood up in front of.

Going down also hands the body to physics — `ActivateRagdoll`, an ordinary
virtual on `ZHM5BaseCharacter` — and then hides it and moves it out from under
the level, restoring it exactly on revive. That last part is blunt, and it is
what works: see §6.

### 3.2 The health the player can see

Absolution shows damage two ways: a red vignette closing in from the edges, and
a ring around the radar.

Taking the health off the engine took both with it. The mod drew its own
vignette from its own pool, and the ring — which reads the engine's health, and
the engine's health cannot move while damage is intercepted — sat at full while
the pool emptied. The HUD told the player they were fine right up to the moment
they fell over.

Both are driven from the mod's pool now. The ring is written at
`ZHUDManager + 0x640`, which is a float only the HUD reads: the engine decides
nothing from it, so it cannot make anybody die or stop them dying.

### 3.3 Seeing each other

Peers are drawn as coloured markers with nameplates, a beam and a facing spur,
interpolated to render time. Distance and ping on the plate.

In **Instinct** — the game's own see-through-walls view — teammates get a
diamond over the head in their colour, at any distance. Instinct is detected by
watching the focus meter fall rather than by reading a key, because the key can
be rebound and the meter cannot. There is an "and only there" option, which is
the better game: co-op stops being a permanent wallhack and knowing where the
others are costs Instinct like everything else does.

### 3.4 Progression

Reaching a checkpoint is announced. Others are pulled forward to it, under a
policy: whoever gets there first pulls everyone, ask me first, or never move me.
Forward only — nobody is ever pulled backwards, because that would delete
progress somebody made.

**This has never fired in practice.** The checkpoint index stayed at 0 through
every session logged, including across a section change. See §7.

### 3.5 Networking

Star topology, host relays. Raw UDP over Winsock — no ENet, no
GameNetworkingSockets. The traffic is a few hundred bytes a second and the
vcpkg manifest belongs to the SDK repository, so a dependency would have cost
more in build fragility than it saved.

Serialisation is explicit, not a struct memcpy: struct padding is a compiler
decision and a wire format must not be. Reads are bounds-checked both ways,
because some of what arrives will be corrupt or hostile.

State goes out unreliably at a fixed rate; events go reliably with
acknowledgement, sequence numbers and duplicate suppression. The host relays
what it receives to everyone except the sender, and an event whose origin is
yourself is dropped so nothing echoes.

Pressing Host asks the router to open the port over UPnP — an SSDP search, one
HTTP GET of the gateway's device description, one SOAP `AddPortMapping` to
whichever of `WANIPConnection` or `WANPPPConnection` it offers. It also asks for
the external address, which is the one thing the other player has to be told,
and puts it in the panel to copy. Closed again on leave and on unload.

Deliberately not Windows' `IUPnPNAT`: that COM interface depends on a service
disabled by default on current Windows, which cannot be enabled from inside a
game.

### 3.6 Following somebody into a mission

Everyone announces their scene every three seconds. If somebody is in a level
you are not in, the panel offers to go.

The first two attempts wrote the host's scene path into `SSceneParameters` and
called `CreateScene`, on the reasoning that this is the game's own transition
path. It returned cleanly and nothing happened, on two machines. It was never
going to: `SSceneParameters` is what the game **tells itself** it is doing — the
level's name, the checkpoint, whether this is a restore — and it is not an input
to the loader. `ZEntitySceneContext` takes its scene through `SetSceneResources`
and `CreateScene` only instantiates what is already set. Writing the path and
asking for a scene put a label on an empty box.

A level is three resources, and the URIs follow a fixed shape:

| | |
|---|---|
| factory | `[<scene>].pc_entitytemplate` |
| blueprint | `[<scene>].pc_entityblueprint` |
| header library | `[[assembly:/common/pc.layoutconfig].pc_layoutdef](<scene>).pc_headerlib` |

Their runtime IDs were in `assets/HashMap.txt` — the 28 MB hash table that ships
with the SDK, which the build omits precisely because nothing looked anything up
in it. Twenty-one levels, sixty-three IDs, and they do not change, so
`SceneTable` carries them outright: nothing is hashed at runtime and the file
still stays out of the build. A scene that is not in the table is refused, since
the failure mode of guessing is tearing the world down and building nothing.

The load spans frames. Resources stream in asynchronously, so `Begin` puts the
three requests in, `Update` watches them, and the player keeps a working game
for the seconds in between. Only when all three are in memory does `CoopMod`
release what it is holding into the old world — the patched vtable entry on the
player, the spectator camera — and commit: `ClearScene(true)`,
`SetSceneResources`, `CreateScene("")`. Every step is logged **before** it runs,
so a crash names the call that caused it.

`StartEntities` is deliberately left to the engine, which starts a scene when it
is whole rather than when it is asked. If the level comes up and nothing in it
moves, that is the line to revisit.

One near miss worth keeping: `ZResourcePtr` declares a copy constructor and a
destructor and **no assignment operator**, so the generated one copies the stub
pointer without taking a reference — and then the source releases on its way
out. Assigning the three resources would have left the mod holding pointers it
did not own and releasing references it never took. They are constructed, not
assigned.

---

## 4. How the mod reports on itself

This changed how the work was done, and it is the most reusable thing here.

Early sessions were: play, watch a panel, photograph the screen, describe it.
That was slow and it did not work — **the SDK takes the keyboard whenever any
of its panels is open**, so a live readout of anything is unreadable by anyone
actually playing. Nobody ever saw a key indicator light up.

So the mod writes down what happens.

**`mods\Coop.log`** — the timeline, flushed every line so a crash keeps it.
Every hit with its whole `SHitInfo` as hex and floats and the address it came
from; every key going down and up; every checkpoint; every actor death and which
of two signals saw it; every transition between up, down and just-up; the
engine's health with the god-mode state on the same line, because a reading of
one without the other is worthless. Plus a summary block near the top answering
everything the panel used to.

**`coop-dump-NNN.txt`** — the facts, written by itself six seconds into the
first level. Build, what the keys compiled to, the player's vtables named by
their own RTTI with entries labelled by module, the actor list, and all 1648
engine configuration variables.

**`mods\Coop-crash.log`** — a vectored exception handler that writes a readable
report before the game's own handler takes over: faulting module and offset,
registers, and every stack address that lands inside a known module. It caught
both `Go there` crashes and named the faulting instruction offset in `Coop.dll`.

The rule that follows: **anything only visible in a panel does not exist.**

---

## 5. What has actually been proven in the game

Not "compiles". Observed, with a log line or a screenshot behind it.

| Thing | Status |
|---|---|
| Build detection, Steam 1.0.447.0 | confirmed |
| Key bindings registered and accepted | confirmed |
| Damage interception armed, slot 5 of 12 | confirmed three ways |
| Our thunk visible in the dumped vtable | confirmed |
| `ZHitman5` RTTI, five vtables, counts match headers | confirmed |
| 256 hits captured, one caller: `HMA.exe+00219A71` | confirmed |
| `SHitInfo` retail layout | confirmed, and the dev headers are wrong |
| Engine removes dead actors from its alive list | confirmed: 19 by vanish, 0 by flag |
| Engine health readable, `115.0 / 115.0` | confirmed |
| Writing the health ring moves it | confirmed |
| No checkpoint reload on death | confirmed |
| Ragdoll, spectator camera, mouse look, self-revive | confirmed |
| Revive timer matches what it displays | confirmed after the fix |
| 1648 configuration variables enumerable | confirmed |
| Frame cost, 6–10 µs average | confirmed |
| **Two machines connected, session held, chat and events relayed** | confirmed |
| Following somebody into a mission | **does not work** |
| Perception suppression stopping engaged guards | **does not work** |
| The progression pull firing | never observed |
| UPnP mapping | untested |
| Peer drawing, instinct diamonds, kill feed with a real peer | untested |
| GOG build, anything | untested |

The networking working at all was the single biggest unknown for the whole
project, and it held on the first try against a second machine.

---

## 6. What went wrong, and what it cost

The most useful section for whoever comes next, because the mistakes cluster.

### Writing against an assumption instead of reading the code

Every one of these was knowable before writing a line, and each was read only
after it failed.

- **`ZString` does not copy.** Its `const char*` constructor sets the top bit of
  the length — the flag for "not allocated" — and keeps the caller's pointer.
  Assigning from an unallocated one passes that pointer through. The engine
  ended up holding a pointer into a `std::string` the mod owns and overwrites
  every three seconds. It killed the game on the first press of `Go there`.
  `ZString::CopyFrom` allocates through the game's own allocator.
- **`HitmanDamageReceivedMultiplier` is not a console variable.** It is a
  per-difficulty array in `HMA.ini` with 58 values. The engine-level backstop
  dispatched into nothing for days. Enumerating all 1648 registered variables in
  the game is what proved it.
- **`CreateScene` does not load anything.** `SetSceneResources` sits beside it
  in the same interface; `CreateScene` only instantiates what is already set.
  This one cost the most: two rounds of in-game testing, a crash on somebody
  else's machine, and three Ghidra scripts hunting for a loader that was never
  missing. The whole answer was in the SDK header the mod already includes, and
  the resource IDs were in a file that ships with the SDK. Reading `.h` before
  writing `.cpp` would have skipped all of it.
- **`SHitInfo`'s published layout is for the 2012 development build.** Retail is
  shifted back by `0x10`. The mod was reading the hit normal and calling it a
  position.

### Handling the transition and not the steady state

- `DownedPhase::Recovering` was set on revive and nothing ever set it back.
  Both `Update` and `OnHitIntercepted` return early unless the phase is `Alive`,
  so the first revive made the player permanently unable to take damage.
- The level was announced only when it changed — so a host already in a mission
  when the session started never announced anything, and the button to follow
  never appeared.
- The menu is a scene like any other, so the player sitting in it announced it,
  which would have offered everybody still playing a button out of their own
  mission.

### Threading, twice, in opposite directions

- The health ring write was on the game thread, which runs *before* the game's
  own HUD pass. The game put its value back every frame. It had to move to
  `OnDrawUI`, inside `Present`, after everything.
- `CreateScene` was called from `OnDrawUI` — mid-frame, renderer state live,
  ImGui half drawn — while it tears the world down and builds another. It had to
  move to the frame update.

Which thread a call belongs on is not something to work out by trying it.

### Reporting silence as proof

`git grep` was run against remote refs that had never been fetched. It returns
nothing, silently. That silence was reported as "the work does not exist", twice,
about work that did exist. `git fetch --all` first.

### Gating features behind a session

The marker key and the kill feed both sat below `if (!session.IsActive())
return;`, so neither did anything at all when testing alone — which is where
everything gets tested first.

### Solving the wrong problem

Guards kept shooting a downed player. The perception switches were found,
written, and confirmed to dispatch — and it changed nothing, because perception
decides whether a guard *finds* a target, not whether it lets one go. A guard
already in combat has you and keeps you. Taking the body away works because
there is then nothing to shoot.

---

## 7. What is left

Ranked by what unlocks the most.

**1. Loading a level — third design, not yet confirmed in the game.** The dev
build's symbols showed the menu's own path: write `SSceneParameters`, set
`ZLevelManager::m_bInSceneTransition`, and the engine's main loop does the rest
— loading screen included. The mod now mounts the level's libraries first (so
nothing is resolved cold), then does exactly that write and that byte. A tracer
logs the flag window on every change, so an ordinary mission start from the
menu proves the offset before anybody bets a session on it. What one run
settles: the trace shows 0→1→0 on a normal start, and Go there rides the same
flag.

**2. Mirroring the engine's health instead of shadowing it.** The offsets are
known and read every frame. Letting the engine take the damage, reading what it
did, and putting the health back before it reaches zero would make close combat,
falls, drowning and scripted kills all count — none of which reach the
interception — and let the game draw its own HUD. It needs the writable current
health, which is a float near the maximum at
`[[base+0xE21358]+0xA2C]+0x21C`; the one-hit probe in Diagnostics is built to
find it.

**3. Real damage numbers.** Every hit costs the same flat amount. The figure is
not in `SHitInfo` — everything past the hit normal was byte-identical across 256
captures — so it is behind the projectile at `+0x0C`, which the mod now captures
`0x80` bytes of on every hit. Four different weapons in one session should
settle it.

**4. Close combat.** Nothing in a close-combat sequence reaches `YouGotHit` at
all, so the player is simply immune to it. `CCHitmanDamage`,
`CCChainFailDamage` and `CCCounterFailDamage` are where it is decided. Solved
for free by (2).

**5. Progression that fires.** The checkpoint index never moved in any logged
session, including across a section change, so "whoever gets there first pulls
everyone" has never had an opportunity to happen. The section is probably the
better unit to synchronise on.

**6. Remote players as characters rather than markers.** `ZHM5LocomotionNetwork`
is the gate, and the notes on it are in `RE-NOTES.md` §3.3. This is the
difference between co-op that reads as a game and co-op that reads as a debug
overlay.

**7. Everything on GOG.** Not one line has been run on it. The god-mode backstop
is explicitly Steam-only and says so.
