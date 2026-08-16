// The last two things co-op wants out of the disassembler.
//
// 1. The damage path. YouGotHit is confirmed at 0x0080df50 (ZHM5BaseCharacter's
//    implementation, IBaseCharacter vtable slot 5). Its callers build the
//    SHitInfo, so the damage figure is computed in there somewhere, and
//    GetBaseDamage should be among what it calls. Finding it turns the mod's
//    flat cost-per-hit into real numbers.
//
// 2. Locomotion. This is the gate on remote players appearing as characters
//    that walk rather than markers that slide. ZHM5LocomotionInput::Update
//    takes stick values and lets the engine animate with its own blending;
//    ZHM5LocomotionNetwork::SendRequest sends Move, Turn, Stop and fifteen
//    others. Neither class carries a type descriptor, so RTTI is no help and
//    the route in is the node-path strings, which are literals in the binary.
//
// Create as HmaCoopTargets.java. Writes hma_targets.txt to your home folder.
//
//@category Absolution

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class HmaCoopTargets extends GhidraScript
{
    // ZHM5BaseCharacter::YouGotHit, confirmed by RTTI vtable recovery.
    private static final long YOU_GOT_HIT = 0x0080df50L;

    private static final int MAX_LISTED = 60;

    private final List<String> lines = new ArrayList<>();

    private Listing listing;

    @Override
    public void run() throws Exception
    {
        listing = currentProgram.getListing();

        emit("Hitman: Absolution co-op targets");
        emit("program " + currentProgram.getName());

        try { damagePath(); }  catch (Exception e) { emit("damage section failed: " + e); }
        try { locomotion(); }  catch (Exception e) { emit("locomotion section failed: " + e); }

        File output = new File(System.getProperty("user.home"), "hma_targets.txt");

        try (PrintWriter writer = new PrintWriter(output, "UTF-8"))
        {
            for (String line : lines) writer.println(line);
        }

        println("");
        println("written to " + output.getAbsolutePath());
    }

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

    private String describe(Function function)
    {
        if (function == null) return "(none)";

        return String.format("%s  %-40s %6d bytes",
            function.getEntryPoint(), function.getName(),
            function.getBody().getNumAddresses());
    }

    // ---- 1. damage --------------------------------------------------------

    private void damagePath()
    {
        heading("1. THE DAMAGE PATH AROUND YouGotHit");

        Address  address  = toAddr(YOU_GOT_HIT);
        Function function = getFunctionAt(address);

        if (function == null)
        {
            emit("Nothing at " + address + ". Wrong build, or analysis incomplete.");
            return;
        }

        emit("YouGotHit: " + describe(function));

        emit("");
        emit("Callers. These construct the SHitInfo, so whatever computes the");
        emit("damage figure is inside one of them:");

        Set<Function> callers = function.getCallingFunctions(monitor);

        if (callers.isEmpty())
        {
            emit("  (none - it is only reached through the vtable, which is");
            emit("   itself the answer: look for code that loads the vtable");
            emit("   and calls slot 5)");
        }

        int shown = 0;

        for (Function caller : callers)
        {
            emit("  " + describe(caller));

            // One level further: the damage accessors should be here.
            for (Function callee : caller.getCalledFunctions(monitor))
            {
                long size = callee.getBody().getNumAddresses();

                // GetBaseDamage and GetExplosionDamage are small accessors.
                if (size > 0 && size < 200)
                {
                    emit("      -> " + describe(callee));
                }
            }

            if (++shown >= 12) break;
        }

        emit("");
        emit("Callees of YouGotHit itself:");

        for (Function callee : function.getCalledFunctions(monitor))
        {
            emit("  " + describe(callee));
        }
    }

    // ---- 2. locomotion ----------------------------------------------------

    private void locomotion()
    {
        heading("2. LOCOMOTION");

        emit("Functions that reference the morpheme node paths and the request");
        emit("names. ZHM5LocomotionNetwork::GetNodeID takes one of these paths,");
        emit("and SendRequest takes a request name, so the code that touches");
        emit("both is the locomotion driver.");

        // Exact strings seen in the earlier string sweep. Substring matched, so
        // near-misses still land.
        String[] needles = {
            "RootNode|FullBody|Locomotion",
            "LocomotionTransitEvents",
            "UpperBodyOverrideWeight",
            "ControlledStates|HumanShield|Move|Strafe",
            "EmotionStates|Combat|Locomotion|Move",
            "StrafeForwardMatchFeet",
            "WalkRunBlend",
            "ZControlledAnimLocomotion",
        };

        // Every function that references any of them, with how many of the
        // strings it touches. A function referencing several is the one that
        // owns the network rather than one that merely mentions a node.
        java.util.Map<String, Integer> hitsByFunction = new java.util.LinkedHashMap<>();
        java.util.Map<String, String>  entryByFunction = new java.util.LinkedHashMap<>();

        Iterator<Data> data = listing.getDefinedData(true);

        int stringsMatched = 0;

        while (data.hasNext() && !monitor.isCancelled())
        {
            Data item = data.next();
            Object value;

            try { value = item.getValue(); } catch (Exception e) { continue; }
            if (value == null) continue;

            String text = value.toString();
            boolean matched = false;

            for (String needle : needles)
            {
                if (text.contains(needle)) { matched = true; break; }
            }

            if (!matched) continue;

            stringsMatched++;

            Reference[] references = getReferencesTo(item.getAddress());
            if (references == null) continue;

            Set<String> seenHere = new LinkedHashSet<>();

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());
                if (owner == null) continue;

                String key = owner.getEntryPoint().toString();

                if (seenHere.add(key))
                {
                    hitsByFunction.merge(key, 1, Integer::sum);
                    entryByFunction.put(key, describe(owner));
                }
            }
        }

        emit("");
        emit(stringsMatched + " matching strings");
        emit("");
        emit("Functions referencing them, most references first. The top entries");
        emit("are the candidates for Init, Update and SendRequest:");
        emit("");

        hitsByFunction.entrySet().stream()
            .sorted((a, b) -> b.getValue() - a.getValue())
            .limit(MAX_LISTED)
            .forEach(entry -> emit(String.format("  refs=%-3d  %s",
                entry.getValue(), entryByFunction.get(entry.getKey()))));

        emit("");
        emit("Next step by hand, if you want it: open the top one, and look for");
        emit("a function taking a float delta plus two float stick axes and");
        emit("several bools. That shape is ZHM5LocomotionInput::Update.");
    }
}
