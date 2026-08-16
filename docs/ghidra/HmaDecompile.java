// Five functions, by address, decompiled.
//
// The previous pass searched for things. This one already knows where they are
// and only needs to read them. Each entry says what it is and why it matters;
// the addresses came out of the search pass and out of a logged session in the
// running game.
//
// Create as HmaDecompile.java. Writes hma_decompile.txt to your home folder.
// Nothing here modifies the program.
//
//@category Absolution

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class HmaDecompile extends GhidraScript
{
    private static class Target
    {
        final long   address;
        final String name;
        final String why;

        Target(long address, String name, String why)
        {
            this.address = address;
            this.name    = name;
            this.why     = why;
        }
    }

    private static final Target[] TARGETS = {

        new Target(0x004fc060L, "the health bar driver",
            "The only function referencing \"_root.g_mcHealthBar\", which is the\n"
          + "ring around the radar. To draw it, it has to read the player's real\n"
          + "health - so the offset the mod has been missing is in here, as a\n"
          + "chain of member reads ending in a float. This is the one that\n"
          + "matters most: with it, the mod can stop keeping a fake health pool\n"
          + "and mirror the engine's instead, which makes the ring move by\n"
          + "itself and makes close combat, falls and drowning count."),

        new Target(0x00806180L, "SHitInfo's filler",
            "Called immediately after the hit struct is initialised, with the\n"
          + "struct and the source object. Everything that varies per hit -\n"
          + "the projectile pointer, the position, the normal - is written by\n"
          + "this. If a damage figure is ever stored in the struct rather than\n"
          + "computed from the projectile later, it is written here."),

        new Target(0x008e97d0L, "the difficulty table setter",
            "Called 130-odd times in a row to register every difficulty\n"
          + "parameter against an index: 5 is HitmanDamageReceivedMultiplier,\n"
          + "0x13 and 0x14 are the close-combat damage values, 0x1E to 0x20 are\n"
          + "Instinct's burn and regen. What we want is the table it writes\n"
          + "into: a global, indexed by those numbers, holding the live values.\n"
          + "Reaching it means the mod can scale received damage through the\n"
          + "engine's own knob instead of a god-mode flag."),

        new Target(0x00629170L, "the weapon code above the hit",
            "The only caller of the function that builds the SHitInfo. Whatever\n"
          + "decides that a shot connected, and with what, is here."),

        new Target(0x00d12da0L, "the busiest Instinct function",
            "Top of the focus-string search by a distance. The mod currently\n"
          + "infers that Instinct is held by watching the meter fall; if a bool\n"
          + "says it outright, it is beside the float this one writes."),
    };

    private final List<String> lines = new ArrayList<>();

    private DecompInterface decompiler;

    @Override
    public void run() throws Exception
    {
        emit("Hitman: Absolution - five functions, decompiled");
        emit("program " + currentProgram.getName());
        emit("image base " + currentProgram.getImageBase());

        decompiler = new DecompInterface();

        if (!decompiler.openProgram(currentProgram))
        {
            emit("");
            emit("The decompiler would not open this program, so this script has");
            emit("nothing to offer. Everything below would have been its output.");
        }

        for (Target target : TARGETS)
        {
            try { handle(target); }
            catch (Exception e) { emit("failed on " + target.name + ": " + e); }
        }

        decompiler.dispose();

        File output = new File(System.getProperty("user.home"), "hma_decompile.txt");

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

    private String describe(Function function)
    {
        if (function == null) return "(none)";

        return String.format("%s  %-40s %6d bytes",
            function.getEntryPoint(), function.getName(),
            function.getBody().getNumAddresses());
    }

    private void handle(Target target) throws Exception
    {
        emit("");
        emit("==========================================================================");
        emit(String.format("%08X  %s", target.address, target.name));
        emit("==========================================================================");
        emit("");

        for (String line : target.why.split("\n"))
        {
            emit(line);
        }

        emit("");

        Address address  = toAddr(target.address);
        Function function = getFunctionAt(address);

        if (function == null)
        {
            function = getFunctionContaining(address);
        }

        if (function == null)
        {
            emit("Nothing at " + address + ". Go there, press D to disassemble,");
            emit("then F to make it a function, and run this again.");

            return;
        }

        emit("  " + describe(function));
        emit("");
        emit("Calls:");

        int shown = 0;

        for (Function callee : function.getCalledFunctions(monitor))
        {
            emit("  " + describe(callee));

            if (++shown >= 30) { emit("  ..."); break; }
        }

        emit("");
        emit("Called by:");

        shown = 0;

        for (Function caller : function.getCallingFunctions(monitor))
        {
            emit("  " + describe(caller));

            if (++shown >= 20) { emit("  ..."); break; }
        }

        emit("");

        DecompileResults results = decompiler.decompileFunction(function, 120, monitor);

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
}
