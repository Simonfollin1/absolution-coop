// Everything the next two phases of co-op would need from the disassembler,
// in one pass, so nobody has to set Ghidra up again to answer one question.
//
// Phase 1 is a remote player who is a character that walks rather than a marker
// that slides. That needs the locomotion network: how to reach it from a
// character, how to feed it stick input, and how to send it movement requests.
//
// Phase 2 is a synchronised world, which needs a way to stop a puppet actor
// thinking for itself.
//
// The route in is that the locomotion *states* carry RTTI even though
// ZHM5LocomotionNetwork does not. They are members of the network, so their
// constructors run inside its Init, and finding who builds them finds the
// network's own layout without guessing at it.
//
// Create as HmaDeepDive.java. Writes hma_deepdive.txt to your home folder.
//
//@category Absolution

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

public class HmaDeepDive extends GhidraScript
{
    // The strongest candidate for ZHM5LocomotionNetwork::Init: 8340 bytes
    // referencing six different morpheme node paths.
    private static final long LOCOMOTION_INIT = 0x007b0d20L;

    // Called from YouGotHit's only static caller. A candidate for a damage
    // accessor, unconfirmed.
    private static final long DAMAGE_CANDIDATE = 0x00707230L;

    // Classes that carry RTTI and matter to phase 1 or 2, taken from the class
    // inventory this project already dumped rather than guessed at.
    private static final String[] CLASSES = {
        // Locomotion state machine. Members of ZHM5LocomotionNetwork, so
        // whoever constructs them is the network's own Init.
        "ZHM5LocomotionRootState",
        "ZHM5LocomotionMoveRootState",
        "ZHM5LocomotionStandRootState",
        "ZHM5LocomotionStrafeRootState",
        "ZHM5LocomotionStrafeStandState",

        // Reads 47's locomotion state for something. If it is cheap to reach,
        // it is a way to observe locomotion without touching the network.
        "ZMonitorHitmanLocomotionStateEntity",

        // What actually moves a character.
        "ZCharacterController",

        // Animation.
        "ZMorphemeEntity",
        "ZAnimatedActor",

        // Phase 2: stopping a puppet from thinking.
        "ZCompiledBehaviorTreeResourceInstaller",
    };

    // Console variables whose readers are the code that owns the system.
    private static final String[] CVAR_ANCHORS = {
        "ai_BehaviorTreeEvaluationsPerFrame",
        "morpheme_DisableAnimation",
        "NPC_MaxCharacterStrafeBlendSpeed",
        "NPC_ActUpperBodyBlendTime",
        "MorphemeHMRunFactor",
    };

    private static final int ENTRIES_PER_TABLE   = 20;
    private static final int MAX_DECOMPILED      = 700;
    private static final int MAX_LISTED          = 30;

    private final List<String> lines = new ArrayList<>();

    private Memory  memory;
    private Listing listing;

    private final TreeMap<String, Address> descriptors = new TreeMap<>();

    @Override
    public void run() throws Exception
    {
        memory  = currentProgram.getMemory();
        listing = currentProgram.getListing();

        emit("Hitman: Absolution deep dive");
        emit("program " + currentProgram.getName());

        collectTypeDescriptors();
        emit(descriptors.size() + " type descriptors");

        try { locomotionInit(); }  catch (Exception e) { fail("locomotion init", e); }
        try { classVtables(); }    catch (Exception e) { fail("class vtables", e); }
        try { cvarOwners(); }      catch (Exception e) { fail("cvar owners", e); }
        try { damageCandidate(); } catch (Exception e) { fail("damage", e); }

        File output = new File(System.getProperty("user.home"), "hma_deepdive.txt");

        try (PrintWriter writer = new PrintWriter(output, "UTF-8"))
        {
            for (String line : lines) writer.println(line);
        }

        println("");
        println("written to " + output.getAbsolutePath());
    }

    private void emit(String text) { lines.add(text); println(text); }

    private void fail(String label, Exception error)
    {
        emit("");
        emit("section '" + label + "' failed: " + error);
    }

    private void heading(String text)
    {
        emit("");
        emit("==========================================================================");
        emit(text);
        emit("==========================================================================");
    }

    private String describe(Function function)
    {
        if (function == null) return "(none)";

        return String.format("%s  %-38s %6d bytes",
            function.getEntryPoint(), function.getName(),
            function.getBody().getNumAddresses());
    }

    // ---- shared RTTI plumbing ---------------------------------------------

    private void collectTypeDescriptors()
    {
        Iterator<Data> data = listing.getDefinedData(true);

        while (data.hasNext() && !monitor.isCancelled())
        {
            Data item = data.next();
            Object value;

            try { value = item.getValue(); } catch (Exception e) { continue; }
            if (value == null) continue;

            String text = value.toString();
            if (!text.startsWith(".?A")) continue;

            String name = text.substring(4);
            if (name.endsWith("@@")) name = name.substring(0, name.length() - 2);

            descriptors.put(name, item.getAddress());
        }
    }

    private Address codePointerAt(Address address)
    {
        int value;
        try { value = memory.getInt(address); } catch (Exception e) { return null; }

        long unsigned = value & 0xFFFFFFFFL;
        if (unsigned == 0) return null;

        Address target;
        try { target = toAddr(unsigned); } catch (Exception e) { return null; }

        MemoryBlock block = memory.getBlock(target);

        return (block == null || !block.isExecute()) ? null : target;
    }

    private List<Address> findPointersTo(Address target) throws Exception
    {
        List<Address> found = new ArrayList<>();

        byte[] pattern = new byte[4];
        int    value   = (int) target.getOffset();

        pattern[0] = (byte) (value & 0xFF);
        pattern[1] = (byte) ((value >> 8) & 0xFF);
        pattern[2] = (byte) ((value >> 16) & 0xFF);
        pattern[3] = (byte) ((value >> 24) & 0xFF);

        for (MemoryBlock block : memory.getBlocks())
        {
            if (!block.isInitialized()) continue;

            Address at = block.getStart();

            while (at != null && !monitor.isCancelled())
            {
                at = memory.findBytes(at, block.getEnd(), pattern, null, true, monitor);
                if (at == null) break;

                found.add(at);

                try { at = at.add(4); } catch (Exception e) { break; }
            }
        }

        return found;
    }

    private String decompile(Function function, int maxLines)
    {
        DecompInterface decompiler = new DecompInterface();

        try
        {
            decompiler.openProgram(currentProgram);

            DecompileResults results =
                decompiler.decompileFunction(function, 180, monitor);

            if (!results.decompileCompleted())
            {
                return "decompilation failed: " + results.getErrorMessage();
            }

            String[] text = results.getDecompiledFunction().getC().split("\n");
            StringBuilder out = new StringBuilder();

            for (int i = 0; i < text.length && i < maxLines; i++)
            {
                out.append("  ").append(text[i]).append("\n");
            }

            if (text.length > maxLines)
            {
                out.append("  ... truncated, ").append(text.length).append(" lines total\n");
            }

            return out.toString();
        }
        finally
        {
            decompiler.dispose();
        }
    }

    // ---- 1. the locomotion network ----------------------------------------

    private void locomotionInit() throws Exception
    {
        heading("1. THE LOCOMOTION NETWORK");

        Address  address  = toAddr(LOCOMOTION_INIT);
        Function function = getFunctionAt(address);

        if (function == null)
        {
            emit("Nothing at " + address + ".");
            return;
        }

        emit("Candidate for ZHM5LocomotionNetwork::Init");
        emit("  " + describe(function));

        emit("");
        emit("Callers. One of these owns the network, so this is how a");
        emit("character reaches it:");

        for (Function caller : function.getCallingFunctions(monitor))
        {
            emit("  " + describe(caller));
        }

        emit("");
        emit("Callees, sorted small to large. GetNodeID, SendRequest and");
        emit("SetControlParameter should be among the smaller ones, and the");
        emit("state constructors among the rest:");

        List<Function> callees = new ArrayList<>(function.getCalledFunctions(monitor));

        callees.sort((a, b) -> Long.compare(a.getBody().getNumAddresses(),
                                            b.getBody().getNumAddresses()));

        int shown = 0;

        for (Function callee : callees)
        {
            emit("  " + describe(callee));
            if (++shown >= MAX_LISTED) { emit("  ... capped"); break; }
        }

        emit("");
        emit("--- decompiled ---");
        emit(decompile(function, MAX_DECOMPILED));
    }

    // ---- 2. vtables and their constructors --------------------------------

    private void classVtables() throws Exception
    {
        heading("2. CLASSES THAT MATTER TO PHASES 1 AND 2");

        emit("For each: its vtable, and the functions that write that vtable.");
        emit("A function writing a class's vtable is constructing it, and for");
        emit("the locomotion states that construction happens inside the");
        emit("network's own Init. Which is how the network's layout falls out");
        emit("without any guessing.");

        for (String className : CLASSES)
        {
            emit("");
            emit("--------------------------------------------------------------");
            emit(className);
            emit("--------------------------------------------------------------");

            Address descriptor = descriptors.get(className);

            if (descriptor == null)
            {
                emit("  no type descriptor");
                continue;
            }

            Address descriptorStart = descriptor.subtract(8);
            int     recovered       = 0;

            for (Address reference : findPointersTo(descriptorStart))
            {
                Address locator;
                try { locator = reference.subtract(0x0C); } catch (Exception e) { continue; }

                int signature;
                try { signature = memory.getInt(locator); } catch (Exception e) { continue; }
                if (signature != 0) continue;

                for (Address slot : findPointersTo(locator))
                {
                    Address vtable;
                    try { vtable = slot.add(4); } catch (Exception e) { continue; }
                    if (codePointerAt(vtable) == null) continue;

                    recovered++;

                    emit("  vtable at " + vtable);

                    for (int index = 0; index < ENTRIES_PER_TABLE; index++)
                    {
                        Address entry  = vtable.add((long) index * 4);
                        Address target = codePointerAt(entry);

                        if (target == null)
                        {
                            emit(String.format("    [%2d] end", index));
                            break;
                        }

                        emit(String.format("    [%2d] %s", index, describe(getFunctionAt(target))));
                    }

                    // Who writes this vtable pointer: the constructors.
                    emit("");
                    emit("  constructed by:");

                    Set<String> seen = new LinkedHashSet<>();
                    int         listed = 0;

                    for (Reference user : getReferencesTo(vtable))
                    {
                        Function owner = getFunctionContaining(user.getFromAddress());
                        if (owner == null) continue;

                        if (seen.add(owner.getEntryPoint().toString()))
                        {
                            emit("    " + describe(owner));
                            if (++listed >= 10) { emit("    ... capped"); break; }
                        }
                    }

                    if (listed == 0)
                    {
                        emit("    (no direct references found)");
                    }
                }
            }

            if (recovered == 0)
            {
                emit("  descriptor found but no vtable points at it");
            }
        }
    }

    // ---- 3. who reads the console variables --------------------------------

    private void cvarOwners()
    {
        heading("3. CONSOLE VARIABLE OWNERS");

        emit("A registered variable's name string is referenced by the code");
        emit("that registers it, which is the system that owns it. Cheapest");
        emit("possible anchor into a subsystem.");

        for (String needle : CVAR_ANCHORS)
        {
            emit("");
            emit("--- " + needle + " ---");

            boolean any = false;

            Iterator<Data> data = listing.getDefinedData(true);

            while (data.hasNext() && !monitor.isCancelled())
            {
                Data item = data.next();
                Object value;

                try { value = item.getValue(); } catch (Exception e) { continue; }
                if (value == null) continue;
                if (!value.toString().contains(needle)) continue;

                Reference[] references = getReferencesTo(item.getAddress());
                if (references == null) continue;

                for (Reference reference : references)
                {
                    Function owner = getFunctionContaining(reference.getFromAddress());
                    if (owner == null) continue;

                    emit("  " + describe(owner));
                    any = true;
                }
            }

            if (!any) emit("  (no referencing function found)");
        }
    }

    // ---- 4. the damage candidate -------------------------------------------

    private void damageCandidate()
    {
        heading("4. THE DAMAGE CANDIDATE");

        emit("FUN_00707230, called from YouGotHit's only static caller. If this");
        emit("reads a projectile's configuration and returns a float, it is");
        emit("what the mod's flat cost-per-hit should be replaced with.");
        emit("");

        Function function = getFunctionAt(toAddr(DAMAGE_CANDIDATE));

        if (function == null)
        {
            emit("Nothing there.");
            return;
        }

        emit(describe(function));
        emit("");
        emit(decompile(function, 120));
    }
}
