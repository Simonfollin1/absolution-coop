// One-pass reconnaissance dump for retail Hitman: Absolution 1.0.447.0.
//
// Java rather than Python because Java is the one scripting language Ghidra
// always has. PyGhidra needs Ghidra to have been launched through pyghidraRun
// with CPython installed, and Jython is an extension that may not be there.
// This needs neither.
//
// IMPORTANT: a Ghidra Java script's class name must match its file name.
// Create the script as HmaRecon.java, or rename the class below to match
// whatever you called it.
//
// Writes hma_recon.txt to your home folder and prints the same to the console.
//
//@category Absolution

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

public class HmaRecon extends GhidraScript
{
    private static final int ENTRIES_PER_TABLE     = 16;
    private static final int MAX_DECOMPILED_LINES  = 300;
    private static final int MAX_STRINGS_PER_GROUP = 60;
    private static final int MAX_CALLERS           = 40;

    private final List<String> lines = new ArrayList<>();

    private long     base;
    private Memory   memory;
    private Listing  listing;

    // Cached once: walking every defined datum is the slowest thing here and
    // three sections want it.
    private List<Object[]> strings = new ArrayList<>();

    @Override
    public void run() throws Exception
    {
        base    = currentProgram.getImageBase().getOffset();
        memory  = currentProgram.getMemory();
        listing = currentProgram.getListing();

        emit("Hitman: Absolution reconnaissance dump");
        emit("program    " + currentProgram.getName());
        emit("image base 0x" + Long.toHexString(base).toUpperCase());
        emit("language   " + currentProgram.getLanguageID());

        collectStrings();

        List<Address> suspects = new ArrayList<>();

        try { verifyKnownAddresses(); }        catch (Exception e) { failed("verify", e); }
        try { checkRtti(); }                   catch (Exception e) { failed("rtti", e); }
        try { suspects = dumpVtables(); }      catch (Exception e) { failed("vtables", e); }
        try { dumpDecompiledConstructor(); }   catch (Exception e) { failed("decompile", e); }
        try { dumpDamagePath(suspects); }      catch (Exception e) { failed("damage", e); }
        try { dumpStringAnchors(); }           catch (Exception e) { failed("strings", e); }
        try { dumpConfigCommands(); }          catch (Exception e) { failed("config", e); }

        File output = new File(System.getProperty("user.home"), "hma_recon.txt");

        try (PrintWriter writer = new PrintWriter(output, "UTF-8"))
        {
            for (String line : lines)
            {
                writer.println(line);
            }
        }

        println("");
        println("======================================================================");
        println("written to " + output.getAbsolutePath());
        println("======================================================================");
    }

    // ---- plumbing ---------------------------------------------------------

    private void emit(String text)
    {
        lines.add(text);
        println(text);
    }

    private void heading(String text)
    {
        emit("");
        emit("==========================================================================");
        emit(text);
        emit("==========================================================================");
    }

    private void failed(String label, Exception error)
    {
        emit("");
        emit("section '" + label + "' failed: " + error);
    }

    private void collectStrings()
    {
        Iterator<Data> data = listing.getDefinedData(true);

        while (data.hasNext() && !monitor.isCancelled())
        {
            Data item = data.next();
            Object value;

            try { value = item.getValue(); } catch (Exception e) { continue; }

            if (value == null)
            {
                continue;
            }

            String text = value.toString();

            if (text.length() < 4 || text.length() > 160)
            {
                continue;
            }

            strings.add(new Object[] { item.getAddress(), text });
        }
    }

    // ---- 1. verify --------------------------------------------------------

    private Map<Long, String> knownFunctions()
    {
        Map<Long, String> known = new LinkedHashMap<>();

        known.put(0x4FF520L, "ZApplicationEngineWin32::MainWindowProc");
        known.put(0x5A7F30L, "ZRenderDevice::Present");
        known.put(0x55A620L, "ZEngineAppCommon::Initialize");
        known.put(0x58E8D0L, "ZHitman5Module::Initialize");
        known.put(0x4479E0L, "ZEntitySceneContext::CreateScene");
        known.put(0x265A80L, "ZEntitySceneContext::ClearScene");
        known.put(0x565200L, "ZEntityManager::ConstructUninitializedEntity");
        known.put(0x21B130L, "ZHitman5::ZHitman5");
        known.put(0x192990L, "ZFreeCameraControlEntity::UpdateCamera");
        known.put(0x82A20L,  "SetPropertyValue");
        known.put(0x576C0L,  "SignalInputPin");
        known.put(0x19BBA0L, "SignalOutputPin");
        known.put(0x212930L, "GetApplicationOptionBool");
        known.put(0x174790L, "ZActor::KillActor");
        known.put(0x388C50L, "ZCheckPointManagerEntity::ActivateJumpPoint");
        known.put(0x2A7C40L, "ZSpatialEntity::GetObjectToWorldMatrix");
        known.put(0x460D00L, "ZSpatialEntity::SetObjectToWorldMatrix");
        known.put(0x379820L, "ZSpatialEntity::GetWorldPosition");
        known.put(0x562060L, "ZSpatialEntity::SetWorldPosition");
        known.put(0x3FA840L, "ZEngineAppCommon::CreateFreeCameraAndControl");
        known.put(0x8CC00L,  "ZGameLoopManager::RegisterForFrameUpdate");
        known.put(0x48A4D0L, "ZInputAction::Digital");
        known.put(0x1C5690L, "ZHUDManager::ShowHUD");
        known.put(0x48B520L, "ZResourceManager::LoadResource");
        known.put(0x555C70L, "ZDynamicResourceLibrary::ZDynamicResourceLibrary");
        known.put(0x52EF10L, "ZConfigCommand::ExecuteCommand");
        known.put(0xD86D0L,  "ZConfigCommand::First");
        known.put(0x3B6390L, "ZHitman5::EquipOutfitResource");
        known.put(0x5476A0L, "ZHM5BaseInventory::AddItemToInventory");
        known.put(0x2C5F50L, "ZInputActionManager::AddBindings");

        return known;
    }

    private void verifyKnownAddresses()
    {
        heading("1. VERIFYING PUBLISHED ADDRESSES");

        emit("If these do not land on real function starts, the binary is not");
        emit("the one the addresses came from and nothing below means anything.");
        emit("");

        Map<Long, String> known = knownFunctions();

        int          hits   = 0;
        List<String> misses = new ArrayList<>();

        for (Map.Entry<Long, String> entry : known.entrySet())
        {
            Address  address  = toAddr(base + entry.getKey());
            Function function = getFunctionAt(address);

            if (function != null)
            {
                hits++;
                continue;
            }

            Instruction instruction = listing.getInstructionAt(address);

            misses.add(String.format("  0x%-8X %-50s %s",
                entry.getKey(), entry.getValue(),
                instruction != null ? "code, but no function defined" : "NOT CODE"));
        }

        emit(hits + " of " + known.size() + " land on a function start");

        if (!misses.isEmpty())
        {
            emit("");
            emit("misses:");

            for (String miss : misses)
            {
                emit(miss);
            }
        }

        if (hits * 2 < known.size())
        {
            emit("");
            emit("*** More than half missed. Either this is not Steam 1.0.447.0,");
            emit("*** or auto-analysis has not finished. Stop here.");
        }
    }

    // ---- 2. RTTI ----------------------------------------------------------

    private void checkRtti()
    {
        heading("2. RTTI");

        List<Object[]> found = new ArrayList<>();

        for (Object[] entry : strings)
        {
            String text = (String) entry[1];

            // MSVC type descriptors are named .?AVClass@@ / .?AUStruct@@
            if (text.contains(".?A"))
            {
                found.add(entry);

                if (found.size() >= 80)
                {
                    break;
                }
            }
        }

        if (found.isEmpty())
        {
            emit("No MSVC type descriptors found.");
            emit("");
            emit("Expected. The scene's class layouts come from leaked PDBs rather");
            emit("than from RTTI, which is consistent with RTTI being off in this");
            emit("build. Class names have to keep coming from the SDK headers.");

            return;
        }

        emit("Found " + found.size() + " type descriptor string(s) - RTTI is present.");
        emit("");
        emit("This would be a significant find: class names, hierarchies and");
        emit("vtable ownership become recoverable straight from the binary,");
        emit("without the leaked development build.");
        emit("");

        for (Object[] entry : found)
        {
            emit("  " + entry[0] + "  " + entry[1]);
        }
    }

    // ---- 3. vtables -------------------------------------------------------

    private Address codePointerAt(Address address)
    {
        int value;

        try { value = memory.getInt(address); } catch (Exception e) { return null; }

        long unsigned = value & 0xFFFFFFFFL;

        if (unsigned == 0)
        {
            return null;
        }

        Address target;

        try { target = toAddr(unsigned); } catch (Exception e) { return null; }

        MemoryBlock block = memory.getBlock(target);

        if (block == null || !block.isExecute())
        {
            return null;
        }

        return target;
    }

    private String[] describe(Address target)
    {
        Function function = getFunctionAt(target);

        if (function == null)
        {
            return new String[] { "(no function)", "0" };
        }

        return new String[] {
            function.getName(),
            Long.toString(function.getBody().getNumAddresses())
        };
    }

    private List<Address> dumpVtables()
    {
        heading("3. ZHitman5 VTABLES");

        emit("A constructor writes one vtable pointer per base sub-object, so");
        emit("all of ZHitman5's tables are referenced from its constructor.");
        emit("");
        emit("What to look for: IComponentInterface's five virtuals - the");
        emit("destructor, GetVariantRef, AddRef, Release, QueryInterface - are");
        emit("all one-liners. So IBaseCharacter's table reads as five tiny");
        emit("functions followed by a substantial one, and that substantial one");
        emit("is YouGotHit. Its index is the answer.");

        List<Address> suspects = new ArrayList<>();

        Address  constructorAddress = toAddr(base + 0x21B130L);
        Function constructor        = getFunctionAt(constructorAddress);

        if (constructor == null)
        {
            constructor = createFunction(constructorAddress, "ZHitman5__ctor");
        }

        if (constructor == null)
        {
            emit("");
            emit("No function at the constructor address - cannot continue.");

            return suspects;
        }

        // Data addresses referenced from the constructor that look like tables
        // of code pointers.
        List<Address>     candidates = new ArrayList<>();
        Set<String>       seen       = new TreeSet<>();
        Iterator<Instruction> instructions =
            listing.getInstructions(constructor.getBody(), true);

        while (instructions.hasNext())
        {
            Instruction instruction = instructions.next();

            for (Reference reference : instruction.getReferencesFrom())
            {
                Address     target = reference.getToAddress();
                MemoryBlock block  = memory.getBlock(target);

                if (block == null || block.isExecute())
                {
                    continue;
                }

                if (codePointerAt(target) == null)
                {
                    continue;
                }

                if (seen.add(target.toString()))
                {
                    candidates.add(target);
                }
            }
        }

        emit("");
        emit(candidates.size() + " vtable candidate(s)");

        for (Address vtable : candidates)
        {
            emit("");
            emit("--- vtable at " + vtable + " ---");

            List<Address> targets = new ArrayList<>();
            List<Long>    sizes   = new ArrayList<>();

            for (int index = 0; index < ENTRIES_PER_TABLE; index++)
            {
                Address entry  = vtable.add((long) index * 4);
                Address target = codePointerAt(entry);

                if (target == null)
                {
                    emit(String.format("  [%2d] end of table", index));
                    break;
                }

                String[] described = describe(target);

                targets.add(target);
                sizes.add(Long.parseLong(described[1]));

                emit(String.format("  [%2d] %s  %-42s %5s bytes",
                    index, target, described[0], described[1]));
            }

            // Five small entries then a large one is the IBaseCharacter shape.
            if (sizes.size() >= 6)
            {
                boolean small = true;

                for (int index = 1; index <= 4; index++)
                {
                    if (sizes.get(index) >= 64)
                    {
                        small = false;
                        break;
                    }
                }

                if (small && sizes.get(5) >= 64)
                {
                    emit("  ^ matches the IBaseCharacter shape: slot 5 is the first");
                    emit("    substantial entry, so that is probably YouGotHit");

                    suspects.add(targets.get(5));
                }
            }
        }

        return suspects;
    }

    private void dumpDecompiledConstructor()
    {
        heading("3b. DECOMPILED ZHitman5 CONSTRUCTOR");

        emit("Shows which offset each vtable pointer is written to, which is");
        emit("how the sub-objects are told apart when two tables look alike.");
        emit("");

        Function constructor = getFunctionAt(toAddr(base + 0x21B130L));

        if (constructor == null)
        {
            emit("No constructor to decompile.");
            return;
        }

        DecompInterface decompiler = new DecompInterface();

        try
        {
            decompiler.openProgram(currentProgram);

            DecompileResults results =
                decompiler.decompileFunction(constructor, 120, new ConsoleTaskMonitor());

            if (!results.decompileCompleted())
            {
                emit("Decompilation failed: " + results.getErrorMessage());
                return;
            }

            String[] text = results.getDecompiledFunction().getC().split("\n");

            for (int index = 0; index < text.length; index++)
            {
                if (index >= MAX_DECOMPILED_LINES)
                {
                    emit("  ... truncated at " + MAX_DECOMPILED_LINES + " lines");
                    break;
                }

                emit("  " + text[index]);
            }
        }
        finally
        {
            decompiler.dispose();
        }
    }

    // ---- 4. damage path ---------------------------------------------------

    private void dumpDamagePath(List<Address> suspects)
    {
        heading("4. THE DAMAGE PATH");

        if (suspects.isEmpty())
        {
            emit("No vtable matched the expected shape, so there is no");
            emit("candidate to trace. Read section 3 by hand: the entry to");
            emit("look for is the first one that is not a one-line accessor.");

            return;
        }

        for (Address suspect : suspects)
        {
            Function function = getFunctionAt(suspect);

            if (function == null)
            {
                continue;
            }

            emit("");
            emit("--- suspected YouGotHit at " + suspect + " (" + function.getName() + ") ---");
            emit("");
            emit("Callers. These build the SHitInfo, so the damage figure is");
            emit("computed somewhere in here:");

            Set<Function> callers = function.getCallingFunctions(monitor);

            if (callers.isEmpty())
            {
                emit("  (none - it is only ever reached through the vtable)");
            }

            int shown = 0;

            for (Function caller : callers)
            {
                emit("  " + caller.getEntryPoint() + "  " + caller.getName());

                if (++shown >= MAX_CALLERS)
                {
                    break;
                }
            }

            emit("");
            emit("Callees. SHitInfo::GetBaseDamage and GetExplosionDamage should");
            emit("be among these if the damage is read inside:");

            shown = 0;

            for (Function callee : function.getCalledFunctions(monitor))
            {
                String[] described = describe(callee.getEntryPoint());

                emit(String.format("  %s  %-42s %5s bytes",
                    callee.getEntryPoint(), described[0], described[1]));

                if (++shown >= MAX_CALLERS)
                {
                    break;
                }
            }
        }
    }

    // ---- 5. string anchors ------------------------------------------------

    private void dumpStringAnchors()
    {
        heading("5. STRING ANCHORS");

        emit("Strings naming engine systems, with how many places reference");
        emit("them. A referenced string is a way into the code that uses it,");
        emit("which is how everything in RE-NOTES.md section 3 gets found.");
        emit("");
        emit(strings.size() + " defined strings in the program");

        String[][] groups = {
            { "locomotion and movement",
              "locomotion", "strafe", "movesm", "turnonspot", "walkrun" },

            { "morpheme animation networks",
              "morpheme", "fullbodysm", "upperbody", "childnetwork", "animnode" },

            { "health and damage",
              "hitpoint", "health", "damage", "hitinfo", "yougothit" },

            { "actors, AI and behaviour trees",
              "behaviortree", "aibt", "aibz", "crowdai", "pathfinder" },

            { "checkpoints and level flow",
              "checkpoint", "jumppoint", "levelmanager", "scenerestart" },

            { "outfits and disguises",
              "outfitkit", "disguise", "zone permit", "contentkit" },
        };

        for (String[] group : groups)
        {
            emit("");
            emit("--- " + group[0] + " ---");

            int shown = 0;

            for (Object[] entry : strings)
            {
                String lowered = ((String) entry[1]).toLowerCase();
                boolean matched = false;

                for (int index = 1; index < group.length; index++)
                {
                    if (lowered.contains(group[index]))
                    {
                        matched = true;
                        break;
                    }
                }

                if (!matched)
                {
                    continue;
                }

                Reference[] references = getReferencesTo((Address) entry[0]);
                int         count      = references == null ? 0 : references.length;

                emit(String.format("  %s  refs=%-3d  %s", entry[0], count, entry[1]));

                if (++shown >= MAX_STRINGS_PER_GROUP)
                {
                    emit("  ... more matches, capped at " + MAX_STRINGS_PER_GROUP);
                    break;
                }
            }

            if (shown == 0)
            {
                emit("  (nothing matched)");
            }
        }
    }

    // ---- 6. console commands ----------------------------------------------

    private void dumpConfigCommands()
    {
        heading("6. CONSOLE COMMANDS");

        emit("ZConfigCommand::ExecuteCommand is reachable from a mod, so every");
        emit("registered command is a lever needing no reverse engineering.");

        Function execute = getFunctionAt(toAddr(base + 0x52EF10L));

        if (execute == null)
        {
            emit("");
            emit("ExecuteCommand not found at its published address.");
            return;
        }

        Set<Function> callers = execute.getCallingFunctions(monitor);

        emit("");
        emit(callers.size() + " function(s) call ExecuteCommand:");

        int shown = 0;

        for (Function caller : callers)
        {
            emit("  " + caller.getEntryPoint() + "  " + caller.getName());

            if (++shown >= MAX_CALLERS)
            {
                break;
            }
        }

        emit("");
        emit("Strings mentioning ConsoleCmd:");

        shown = 0;

        for (Object[] entry : strings)
        {
            if (!((String) entry[1]).toLowerCase().contains("consolecmd"))
            {
                continue;
            }

            emit("  " + entry[0] + "  " + entry[1]);

            if (++shown >= MAX_STRINGS_PER_GROUP)
            {
                break;
            }
        }

        if (shown == 0)
        {
            emit("  (none by that name - commands are probably registered from");
            emit("   constructors rather than from a literal table)");
        }
    }
}
