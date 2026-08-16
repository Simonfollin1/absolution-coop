# One-pass reconnaissance dump for retail Hitman: Absolution 1.0.447.0.
#
# Run in Ghidra's Script Manager once HMA.exe is imported and fully analysed.
# Writes hma_recon.txt next to your home folder and prints the same to console.
#
# Six sections, in order of how much each unlocks:
#
#   1. Verify   - do the ~110 published addresses land on real function starts?
#                 If they do not, everything downstream is against the wrong
#                 binary and nothing else in this file means anything.
#   2. RTTI     - does the binary carry MSVC type descriptors? The modding
#                 scene's class layouts come from leaked PDBs, which implies
#                 no. Worth thirty seconds to find out, because if they are
#                 there, class names can be recovered without the leak.
#   3. Vtables  - ZHitman5's base sub-object vtables, to settle which slot
#                 IBaseCharacter::YouGotHit occupies.
#   4. Damage   - whatever calls and is called by the suspected YouGotHit,
#                 which is where SHitInfo and its damage accessors live.
#   5. Anchors  - strings that name engine systems, with reference counts.
#                 These are the entry points for finding anything later:
#                 locomotion, morpheme, health, behaviour trees.
#   6. Config   - console command names, which say how much of the engine can
#                 be driven without any code at all.
#
# Every section is independent and wrapped, so one failure does not lose the
# rest of the run.

import os
import re

BASE = currentProgram.getImageBase().getOffset()

ENTRIES_PER_TABLE = 16
MAX_DECOMPILED_LINES = 300
MAX_STRINGS_PER_GROUP = 60
MAX_CALLERS = 40

memory = currentProgram.getMemory()
listing = currentProgram.getListing()

lines = []


def emit(text):
    lines.append(str(text))
    print(text)


def heading(text):
    emit("")
    emit("=" * 74)
    emit(text)
    emit("=" * 74)


# Addresses the HitmanAbsolutionSDK calls on this build. Section 1 checks them.
KNOWN_FUNCTIONS = {
    0x4FF520: "ZApplicationEngineWin32::MainWindowProc",
    0x5A7F30: "ZRenderDevice::Present",
    0x55A620: "ZEngineAppCommon::Initialize",
    0x58E8D0: "ZHitman5Module::Initialize",
    0x4479E0: "ZEntitySceneContext::CreateScene",
    0x265A80: "ZEntitySceneContext::ClearScene",
    0x565200: "ZEntityManager::ConstructUninitializedEntity",
    0x21B130: "ZHitman5::ZHitman5",
    0x192990: "ZFreeCameraControlEntity::UpdateCamera",
    0x82A20:  "SetPropertyValue",
    0x576C0:  "SignalInputPin",
    0x19BBA0: "SignalOutputPin",
    0x212930: "GetApplicationOptionBool",
    0x174790: "ZActor::KillActor",
    0x388C50: "ZCheckPointManagerEntity::ActivateJumpPoint",
    0x2A7C40: "ZSpatialEntity::GetObjectToWorldMatrix",
    0x460D00: "ZSpatialEntity::SetObjectToWorldMatrix",
    0x379820: "ZSpatialEntity::GetWorldPosition",
    0x562060: "ZSpatialEntity::SetWorldPosition",
    0x3FA840: "ZEngineAppCommon::CreateFreeCameraAndControl",
    0x8CC00:  "ZGameLoopManager::RegisterForFrameUpdate",
    0x48A4D0: "ZInputAction::Digital",
    0x1C5690: "ZHUDManager::ShowHUD",
    0x48B520: "ZResourceManager::LoadResource",
    0x555C70: "ZDynamicResourceLibrary::ZDynamicResourceLibrary",
    0x52EF10: "ZConfigCommand::ExecuteCommand",
    0xD86D0:  "ZConfigCommand::First",
    0x3B6390: "ZHitman5::EquipOutfitResource",
    0x5476A0: "ZHM5BaseInventory::AddItemToInventory",
    0x2C5F50: "ZInputActionManager::AddBindings",
}

# Section 5 groups. Each is a list of case-insensitive substrings.
STRING_GROUPS = [
    ("locomotion and movement",
     ["locomotion", "LocomotionSM", "strafe", "movesm", "turnonspot"]),

    ("morpheme animation networks",
     ["morpheme", "fullbodysm", "upperbody", "childnetwork", "animnode"]),

    ("health and damage",
     ["hitpoint", "health", "damage", "hitinfo", "youGotHit"]),

    ("actors, AI and behaviour trees",
     ["behaviortree", "aibt", "aibz", "actorsm", "crowdai", "pathfinder"]),

    ("checkpoints and level flow",
     ["checkpoint", "jumppoint", "scenerestart", "levelmanager"]),

    ("outfits and disguises",
     ["outfitkit", "disguise", "zone permit", "contentkit"]),
]


# ---------------------------------------------------------------------------
# 1. Verify the published addresses
# ---------------------------------------------------------------------------

def verify_known_addresses():
    heading("1. VERIFYING PUBLISHED ADDRESSES")

    hits = 0
    misses = []

    for rva in sorted(KNOWN_FUNCTIONS):
        name = KNOWN_FUNCTIONS[rva]
        address = toAddr(BASE + rva)
        function = getFunctionAt(address)

        if function is not None:
            hits += 1
        else:
            instruction = listing.getInstructionAt(address)
            misses.append((rva, name, instruction is not None))

    emit("%d of %d land on a function start" % (hits, len(KNOWN_FUNCTIONS)))

    if misses:
        emit("")
        emit("not on a function start:")

        for rva, name, is_code in misses:
            emit("  0x%-8X %-52s %s" % (
                rva, name, "code, no function defined" if is_code else "NOT CODE"))

    if hits < len(KNOWN_FUNCTIONS) / 2:
        emit("")
        emit("*** More than half missed. This is probably not the Steam")
        emit("*** 1.0.447.0 build these addresses come from, or analysis has")
        emit("*** not finished. Nothing below can be trusted.")


# ---------------------------------------------------------------------------
# 2. RTTI
# ---------------------------------------------------------------------------

def check_rtti():
    heading("2. RTTI")

    # MSVC mangles type descriptor names as .?AVClassName@@ / .?AUStructName@@
    found = []

    for data in listing.getDefinedData(True):
        try:
            value = data.getValue()
        except:
            continue

        if value is None:
            continue

        text = str(value)

        if ".?A" in text:
            found.append((data.getAddress(), text))

            if len(found) >= 80:
                break

    if not found:
        emit("No MSVC type descriptors found.")
        emit("")
        emit("Expected: the scene's class layouts come from leaked PDBs rather")
        emit("than from RTTI, which is consistent with RTTI being off in this")
        emit("build. Class names have to come from the SDK headers.")
        return

    emit("Found %d type descriptor string(s) - RTTI appears to be present." % len(found))
    emit("")
    emit("That would be a significant find: class names, hierarchies and")
    emit("vtable ownership become recoverable directly from the binary.")
    emit("")

    for address, text in found:
        emit("  %s  %s" % (address, text))


# ---------------------------------------------------------------------------
# 3 and 4. Vtables, and the damage path
# ---------------------------------------------------------------------------

def code_pointer_at(address):
    try:
        value = memory.getInt(address) & 0xFFFFFFFF
    except:
        return None

    if value == 0:
        return None

    target = toAddr(value)
    block = memory.getBlock(target)

    if block is None or not block.isExecute():
        return None

    return target


def describe(target):
    function = getFunctionAt(target)

    if function is None:
        return ("(no function)", 0)

    return (function.getName(), function.getBody().getNumAddresses())


def vtable_candidates(function):
    found = []

    for instruction in listing.getInstructions(function.getBody(), True):
        for reference in instruction.getReferencesFrom():
            target = reference.getToAddress()
            block = memory.getBlock(target)

            if block is None or block.isExecute():
                continue

            if code_pointer_at(target) is None:
                continue

            if target not in found:
                found.append(target)

    return found


def dump_vtables():
    heading("3. ZHitman5 VTABLES")

    emit("A constructor writes one vtable pointer per base sub-object, so all")
    emit("of ZHitman5's tables are referenced from its constructor.")
    emit("")
    emit("What to look for: IComponentInterface's five virtuals - destructor,")
    emit("GetVariantRef, AddRef, Release, QueryInterface - are all one-liners.")
    emit("So IBaseCharacter's table reads as five tiny functions followed by a")
    emit("substantial one. That substantial one is YouGotHit, and its index is")
    emit("the number this whole exercise is for.")

    address = toAddr(BASE + 0x21B130)
    function = getFunctionAt(address)

    if function is None:
        function = createFunction(address, "ZHitman5__ctor")

    if function is None:
        emit("")
        emit("No function at the constructor address - cannot continue.")
        return []

    candidates = vtable_candidates(function)

    emit("")
    emit("%d vtable candidate(s)" % len(candidates))

    suspects = []

    for vtable in candidates:
        emit("")
        emit("--- vtable at %s ---" % vtable)

        entries = []

        for index in range(ENTRIES_PER_TABLE):
            entry = vtable.add(index * 4)
            target = code_pointer_at(entry)

            if target is None:
                emit("  [%2d] end of table" % index)
                break

            name, size = describe(target)
            entries.append((index, target, name, size))

            emit("  [%2d] %s  %-42s %5d bytes" % (index, target, name, size))

        # Five small entries then a large one is the IBaseCharacter shape.
        if len(entries) >= 6:
            small = all(entry[3] < 64 for entry in entries[1:5])
            large = entries[5][3] >= 64

            if small and large:
                emit("  ^ matches the IBaseCharacter shape: slot 5 is the")
                emit("    first substantial entry, i.e. probably YouGotHit")
                suspects.append(entries[5][1])

    return suspects


def dump_damage_path(suspects):
    heading("4. THE DAMAGE PATH")

    if not suspects:
        emit("No vtable matched the expected shape, so there is no candidate")
        emit("to trace. Read section 3 by hand: the entry to look for is the")
        emit("first one that is not a one-line accessor.")
        return

    for suspect in suspects:
        function = getFunctionAt(suspect)

        if function is None:
            continue

        emit("")
        emit("--- suspected YouGotHit at %s (%s) ---" % (suspect, function.getName()))
        emit("")
        emit("Callers - these build the SHitInfo, so the damage figure is")
        emit("computed somewhere in here:")

        callers = list(function.getCallingFunctions(None))

        for caller in callers[:MAX_CALLERS]:
            emit("  %s  %s" % (caller.getEntryPoint(), caller.getName()))

        if not callers:
            emit("  (none found - it is only reached through the vtable)")

        emit("")
        emit("Callees - SHitInfo::GetBaseDamage and GetExplosionDamage should")
        emit("be among these if the damage is read inside:")

        for callee in list(function.getCalledFunctions(None))[:MAX_CALLERS]:
            name, size = describe(callee.getEntryPoint())
            emit("  %s  %-42s %5d bytes" % (callee.getEntryPoint(), name, size))


def dump_decompiled_constructor():
    heading("3b. DECOMPILED ZHitman5 CONSTRUCTOR")

    emit("Shows which offset each vtable pointer is written to, which is how")
    emit("the sub-objects are told apart when two tables look alike.")
    emit("")

    try:
        from ghidra.app.decompiler import DecompInterface
        from ghidra.util.task import ConsoleTaskMonitor
    except:
        emit("Decompiler unavailable.")
        return

    function = getFunctionAt(toAddr(BASE + 0x21B130))

    if function is None:
        return

    interface = DecompInterface()
    interface.openProgram(currentProgram)

    result = interface.decompileFunction(function, 120, ConsoleTaskMonitor())

    if not result.decompileCompleted():
        emit("Decompilation failed: %s" % result.getErrorMessage())
        return

    text = result.getDecompiledFunction().getC()

    for number, line in enumerate(text.split("\n")):
        if number >= MAX_DECOMPILED_LINES:
            emit("  ... truncated at %d lines" % MAX_DECOMPILED_LINES)
            break

        emit("  " + line)


# ---------------------------------------------------------------------------
# 5. String anchors
# ---------------------------------------------------------------------------

def dump_string_anchors():
    heading("5. STRING ANCHORS")

    emit("Strings naming engine systems, with how many places reference them.")
    emit("A string with references is a way into the code that uses it, which")
    emit("is how everything else in RE-NOTES.md gets found.")

    collected = []

    for data in listing.getDefinedData(True):
        try:
            value = data.getValue()
        except:
            continue

        if value is None:
            continue

        text = str(value)

        if len(text) < 4 or len(text) > 120:
            continue

        collected.append((data.getAddress(), text))

    emit("")
    emit("%d defined strings in the program" % len(collected))

    for title, needles in STRING_GROUPS:
        emit("")
        emit("--- %s ---" % title)

        shown = 0

        for address, text in collected:
            lowered = text.lower()

            if not any(needle.lower() in lowered for needle in needles):
                continue

            references = getReferencesTo(address)
            count = len(references) if references is not None else 0

            emit("  %s  refs=%-3d  %s" % (address, count, text))

            shown += 1

            if shown >= MAX_STRINGS_PER_GROUP:
                emit("  ... more matches, capped at %d" % MAX_STRINGS_PER_GROUP)
                break

        if shown == 0:
            emit("  (nothing matched)")


# ---------------------------------------------------------------------------
# 6. Console commands
# ---------------------------------------------------------------------------

def dump_config_commands():
    heading("6. CONSOLE COMMANDS")

    emit("ZConfigCommand::ExecuteCommand is reachable from a mod, so every")
    emit("registered command is a lever that needs no reverse engineering at")
    emit("all. These are strings referenced near the command machinery.")

    execute = getFunctionAt(toAddr(BASE + 0x52EF10))

    if execute is None:
        emit("")
        emit("ZConfigCommand::ExecuteCommand not found at its published address.")
        return

    callers = list(execute.getCallingFunctions(None))

    emit("")
    emit("%d function(s) call ExecuteCommand:" % len(callers))

    for caller in callers[:MAX_CALLERS]:
        emit("  %s  %s" % (caller.getEntryPoint(), caller.getName()))

    emit("")
    emit("Command-shaped strings (ConsoleCmd, cvar-like names):")

    shown = 0
    pattern = re.compile(r"^[A-Za-z][A-Za-z0-9_]{3,40}$")

    for data in listing.getDefinedData(True):
        try:
            value = data.getValue()
        except:
            continue

        if value is None:
            continue

        text = str(value)

        if "ConsoleCmd" not in text and "consolecmd" not in text.lower():
            continue

        emit("  %s  %s" % (data.getAddress(), text))

        shown += 1

        if shown >= MAX_STRINGS_PER_GROUP:
            break

    if shown == 0:
        emit("  (none found by name - they are probably registered from")
        emit("   constructors rather than from a literal table)")


# ---------------------------------------------------------------------------

emit("Hitman: Absolution reconnaissance dump")
emit("program    %s" % currentProgram.getName())
emit("image base 0x%X" % BASE)
emit("language   %s" % currentProgram.getLanguageID())

sections = [
    ("verify", verify_known_addresses),
    ("rtti", check_rtti),
]

for label, runner in sections:
    try:
        runner()
    except Exception as error:
        emit("")
        emit("section '%s' failed: %s" % (label, error))

try:
    suspects = dump_vtables()
except Exception as error:
    emit("section 'vtables' failed: %s" % error)
    suspects = []

for label, runner in [("decompile", dump_decompiled_constructor),
                      ("strings", dump_string_anchors),
                      ("config", dump_config_commands)]:
    try:
        runner()
    except Exception as error:
        emit("")
        emit("section '%s' failed: %s" % (label, error))

try:
    dump_damage_path(suspects)
except Exception as error:
    emit("")
    emit("section 'damage' failed: %s" % error)

output_path = os.path.join(os.path.expanduser("~"), "hma_recon.txt")

with open(output_path, "w") as handle:
    handle.write("\n".join(lines))

print("")
print("=" * 74)
print("written to " + output_path)
print("=" * 74)
