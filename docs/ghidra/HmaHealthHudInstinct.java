// The four things the game itself could not answer.
//
// A logged session in the running game settled where the damage comes from and
// what a hit contains. What it could not settle is what the code around it
// does, and that is all in here:
//
//   1. THE DAMAGE FUNCTION. Every hit taken in a session - all 256 of them -
//      came from HMA.exe+0x00219A71, which is 0x00619A71 here. That function
//      builds the SHitInfo and calls YouGotHit through the vtable, so it is
//      also where the damage figure is computed. It has never been read.
//
//   2. THE HEALTH RING. The HUD has one, around the radar, and the mod has been
//      emptying a pool it knows nothing about. _root.g_mcHealthBar is in the
//      strings; whoever references it is the code that drives the ring, and
//      what that code reads is the engine's real health.
//
//   3. CLOSE COMBAT. Nothing in a close-combat sequence reaches YouGotHit, so
//      the mod cannot see that damage at all and the player is simply immune.
//      CCHitmanDamage, CCChainFailDamage and CCCounterFailDamage are where it
//      is decided.
//
//   4. INSTINCT. ZHM5FocusController holds a float* to the meter. The mod
//      guesses "being held" from the value falling, which works but is a guess;
//      somewhere there is a flag that says it outright.
//
// Create as HmaHealthHudInstinct.java. Writes hma_health.txt to your home
// folder. Nothing here modifies the program.
//
//@category Absolution

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
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

public class HmaHealthHudInstinct extends GhidraScript
{
    // Measured in the running game: the one and only caller of YouGotHit, as an
    // offset from the module base. The image loads at 0x400000, so this is the
    // address to open here.
    private static final long DAMAGE_CALLER = 0x00619A71L;

    private static final int MAX_LISTED = 40;

    private final List<String> lines = new ArrayList<>();

    private Listing listing;
    private DecompInterface decompiler;

    @Override
    public void run() throws Exception
    {
        listing = currentProgram.getListing();

        emit("Hitman: Absolution - health, HUD, close combat, instinct");
        emit("program " + currentProgram.getName());
        emit("image base " + currentProgram.getImageBase());
        emit("");
        emit("If the image base above is not 00400000, every address in here is");
        emit("off by the difference - the numbers came from a running process.");

        startDecompiler();

        try { damageFunction(); }  catch (Exception e) { emit("section 1 failed: " + e); }
        try { healthRing(); }      catch (Exception e) { emit("section 2 failed: " + e); }
        try { closeCombat(); }     catch (Exception e) { emit("section 3 failed: " + e); }
        try { instinct(); }        catch (Exception e) { emit("section 4 failed: " + e); }

        stopDecompiler();

        File output = new File(System.getProperty("user.home"), "hma_health.txt");

        try (PrintWriter writer = new PrintWriter(output, "UTF-8"))
        {
            for (String line : lines) writer.println(line);
        }

        println("");
        println("written to " + output.getAbsolutePath());
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
        emit("");
    }

    private String describe(Function function)
    {
        if (function == null) return "(none)";

        return String.format("%s  %-44s %6d bytes",
            function.getEntryPoint(), function.getName(),
            function.getBody().getNumAddresses());
    }

    private void startDecompiler()
    {
        try
        {
            decompiler = new DecompInterface();
            decompiler.openProgram(currentProgram);
        }
        catch (Exception e)
        {
            decompiler = null;
            emit("decompiler unavailable (" + e + ") - listings only");
        }
    }

    private void stopDecompiler()
    {
        if (decompiler != null)
        {
            try { decompiler.dispose(); } catch (Exception ignored) { }
        }
    }

    private void decompile(Function function, String why)
    {
        if (function == null) return;

        emit("");
        emit("--- " + why + ": " + function.getName() + " at " + function.getEntryPoint());
        emit("");

        if (decompiler == null)
        {
            emit("(no decompiler)");
            return;
        }

        try
        {
            DecompileResults results = decompiler.decompileFunction(function, 90, monitor);

            if (results == null || !results.decompileCompleted())
            {
                emit("(decompilation failed: "
                    + (results == null ? "no result" : results.getErrorMessage()) + ")");

                return;
            }

            for (String line : results.getDecompiledFunction().getC().split("\n"))
            {
                emit("    " + line);
            }
        }
        catch (Exception e)
        {
            emit("(decompilation threw " + e + ")");
        }
    }

    // Every function that references any string containing one of the needles,
    // most matches first. The workhorse of the last three sections.
    private Map<String, Function> functionsReferencingStrings(String[] needles, String label)
    {
        Map<String, Integer>  hits    = new LinkedHashMap<>();
        Map<String, Function> byEntry = new LinkedHashMap<>();

        Iterator<Data> data = listing.getDefinedData(true);

        int matched = 0;

        while (data.hasNext() && !monitor.isCancelled())
        {
            Data item = data.next();

            DataType type = item.getDataType();

            if (type == null) continue;

            String typeName = type.getName().toLowerCase();

            if (!typeName.contains("string") && !typeName.contains("char")) continue;

            Object value;

            try { value = item.getValue(); } catch (Exception e) { continue; }
            if (value == null) continue;

            String text = value.toString();
            String hit  = null;

            for (String needle : needles)
            {
                if (text.contains(needle)) { hit = needle; break; }
            }

            if (hit == null) continue;

            matched++;

            emit(String.format("  \"%s\" at %s", text, item.getAddress()));

            Reference[] references = getReferencesTo(item.getAddress());

            if (references == null || references.length == 0)
            {
                emit("      (no references - pushed as an immediate, most likely)");
                continue;
            }

            Set<String> seenHere = new LinkedHashSet<>();

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());

                if (owner == null)
                {
                    emit("      referenced from " + reference.getFromAddress()
                        + " (not inside a function)");

                    continue;
                }

                String key = owner.getEntryPoint().toString();

                if (seenHere.add(key))
                {
                    hits.merge(key, 1, Integer::sum);
                    byEntry.put(key, owner);

                    emit("      <- " + describe(owner));
                }
            }
        }

        emit("");
        emit(matched + " matching strings for " + label);

        return byEntry;
    }

    // ---- 1. the damage function -------------------------------------------

    private void damageFunction()
    {
        heading("1. WHERE THE DAMAGE IS COMPUTED");

        emit("Measured in the game: all 256 hits in a session came from");
        emit("HMA.exe+00219A71. That is this address, and this is the function");
        emit("that owns it. It builds the SHitInfo and calls YouGotHit through");
        emit("the vtable, so the damage figure is decided in here.");
        emit("");

        Address address = toAddr(DAMAGE_CALLER);
        Function owner  = getFunctionContaining(address);

        if (owner == null)
        {
            emit("Nothing contains " + address + ".");
            emit("Either analysis has not reached it, or the image base differs.");
            emit("Try 'Disassemble' at that address and run this again.");

            return;
        }

        emit("The call site sits at " + address + ", inside:");
        emit("  " + describe(owner));

        emit("");
        emit("What it calls. GetBaseDamage should be one of the small ones, and");
        emit("anything returning a float is a candidate:");

        int shown = 0;

        for (Function callee : owner.getCalledFunctions(monitor))
        {
            long size = callee.getBody().getNumAddresses();

            String returns = callee.getReturnType() == null
                ? "?" : callee.getReturnType().getName();

            emit(String.format("  %-8s %s", returns, describe(callee)));

            if (++shown >= MAX_LISTED) { emit("  ..."); break; }
        }

        emit("");
        emit("Who calls it, which is the weapon or melee code above it:");

        shown = 0;

        for (Function caller : owner.getCallingFunctions(monitor))
        {
            emit("  " + describe(caller));

            if (++shown >= MAX_LISTED) { emit("  ..."); break; }
        }

        decompile(owner, "the damage path");
    }

    // ---- 2. the health ring ------------------------------------------------

    private void healthRing()
    {
        heading("2. THE HEALTH RING AROUND THE RADAR");

        emit("It is a Scaleform movieclip. Whoever references its name is the");
        emit("code that drives it, and what that code reads is the engine's own");
        emit("health - which is the offset the mod is missing.");
        emit("");

        String[] needles = {
            "g_mcHealthBar",
            "g_mcHealth",
            "mcHealth",
            "HealthBar",
            "_root.g_mc",
        };

        Map<String, Function> owners = functionsReferencingStrings(needles, "the health ring");

        emit("");
        emit("Decompiling the ones that reference them:");

        int shown = 0;

        for (Function function : owners.values())
        {
            decompile(function, "health ring");

            if (++shown >= 3) { emit(""); emit("(stopping after three)"); break; }
        }
    }

    // ---- 3. close combat ---------------------------------------------------

    private void closeCombat()
    {
        heading("3. CLOSE COMBAT DAMAGE");

        emit("Nothing in a close-combat sequence reaches YouGotHit - a whole");
        emit("session of them produced not one call - so the mod cannot see that");
        emit("damage and the player is immune to it. These are the names the");
        emit("engine registers for it, and whoever reads them is the path.");
        emit("");

        String[] needles = {
            "CCHitmanDamage",
            "CCChainFailDamage",
            "CCCounterFailDamage",
            "CCSanchezChainFailDamage",
            "CCSanchezCounterFailDamage",
        };

        Map<String, Function> owners = functionsReferencingStrings(needles, "close combat");

        int shown = 0;

        for (Function function : owners.values())
        {
            decompile(function, "close combat damage");

            if (++shown >= 3) { emit(""); emit("(stopping after three)"); break; }
        }
    }

    // ---- 4. instinct -------------------------------------------------------

    private void instinct()
    {
        heading("4. INSTINCT");

        emit("The mod reads the meter through ZHM5FocusController's float* and");
        emit("infers 'being held' from the value falling. That works and it is");
        emit("still a guess - somewhere a flag says it outright. Anything here");
        emit("that reads the same float and sets a bool is that flag.");
        emit("");

        String[] needles = {
            "Focus",
            "focus",
            "Instinct",
            "instinct",
        };

        // Deliberately narrower output than the others: "focus" appears
        // everywhere, so only functions referencing more than one are worth
        // printing, and none are decompiled automatically.
        Map<String, Integer>  counts  = new LinkedHashMap<>();
        Map<String, Function> byEntry = new LinkedHashMap<>();

        Iterator<Data> data = listing.getDefinedData(true);

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

            Reference[] references = getReferencesTo(item.getAddress());
            if (references == null) continue;

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());
                if (owner == null) continue;

                String key = owner.getEntryPoint().toString();

                counts.merge(key, 1, Integer::sum);
                byEntry.put(key, owner);
            }
        }

        emit("Functions touching more than one focus-related string, most first.");
        emit("The focus controller's own methods should be near the top:");
        emit("");

        counts.entrySet().stream()
            .filter(entry -> entry.getValue() > 1)
            .sorted((a, b) -> b.getValue() - a.getValue())
            .limit(MAX_LISTED)
            .forEach(entry -> emit(String.format("  refs=%-3d  %s",
                entry.getValue(), describe(byEntry.get(entry.getKey())))));

        emit("");
        emit("By hand from here: open the top one, find the float it writes");
        emit("through a pointer member, and look for the bool beside it.");
    }
}
