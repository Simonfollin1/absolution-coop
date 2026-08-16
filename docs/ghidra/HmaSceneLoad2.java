// The level loader, approached from the data rather than the code.
//
// The first pass went looking for callers of CreateScene and found a destructor,
// because Ghidra has no function at that address and handed back whatever
// surrounds it. It did turn up something better by accident: the strings.
//
//     "assembly:/Scenes/Menu/Menu_Main.entity"   FUN_0070ca40, FUN_008b4f10
//     "assembly:/scenes/menu/menu_main.entity"   FUN_00838460
//     "assembly:/scenes/benchmark/benchmark..."  FUN_0060df10
//
// Going to the main menu is a level load. Same code path, different string. And
// the benchmark is the same thing again with nothing else attached, which makes
// it the smallest complete example of a scene being loaded from a path.
//
// The better anchor is the data. SSceneParameters is ZLevelManager + 0x4, and
// the SDK resolves ZLevelManager at base + 0xE21310, so:
//
//     0x01221314   sSceneResource      the scene path, a ZString
//     0x0122131C   eGameMode
//     0x0122132C   nCheckpointIndex
//
// Whoever writes 0x01221314 is the menu picking a mission. Whoever reads it is
// the loader. Both are exactly what the mod needs, and neither depends on
// Ghidra having got the function boundaries right.
//
// Create as HmaSceneLoad2.java. Writes hma_sceneload2.txt to your home folder.
//
//@category Absolution

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class HmaSceneLoad2 extends GhidraScript
{
    // SSceneParameters, field by field, at its absolute address.
    private static final long[] FIELDS = {
        0x01221314L,   // sSceneResource   (ZString: length then chars)
        0x01221318L,   // sSceneResource   chars pointer
        0x0122131CL,   // eGameMode
        0x0122132CL,   // nCheckpointIndex
    };

    private static final String[] FIELD_NAMES = {
        "sSceneResource.length",
        "sSceneResource.chars",
        "eGameMode",
        "nCheckpointIndex",
    };

    // The functions the string search already found. Decompiled outright,
    // because one of them is the road into the menu and therefore the road
    // into a level.
    private static final long[] KNOWN = {
        0x0070ca40L,   // references "assembly:/Scenes/Menu/Menu_Main.entity"
        0x008b4f10L,   // the same string, smaller
        0x00838460L,   // the lowercase variant
        0x0060df10L,   // the benchmark scene - the smallest complete example
    };

    private final List<String> lines = new ArrayList<>();

    private DecompInterface decompiler;

    @Override
    public void run() throws Exception
    {
        emit("Hitman: Absolution - the level loader, from the data side");
        emit("program " + currentProgram.getName());
        emit("image base " + currentProgram.getImageBase());

        decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        try { fields(); } catch (Exception e) { emit("field search failed: " + e); }
        try { known(); }  catch (Exception e) { emit("known functions failed: " + e); }

        decompiler.dispose();

        File output = new File(System.getProperty("user.home"), "hma_sceneload2.txt");

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
        emit("");
    }

    private String describe(Function function)
    {
        if (function == null) return "(none)";

        return String.format("%s  %-40s %6d bytes",
            function.getEntryPoint(), function.getName(),
            function.getBody().getNumAddresses());
    }

    private void decompile(Function function, String why)
    {
        if (function == null) return;

        emit("");
        emit("--- " + why + ": " + function.getName() + " at " + function.getEntryPoint());
        emit("");

        try
        {
            DecompileResults results = decompiler.decompileFunction(function, 120, monitor);

            if (results == null || !results.decompileCompleted())
            {
                emit("(decompilation failed)");
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

    // ---- 1. who touches the scene parameters --------------------------------

    private void fields() throws Exception
    {
        heading("1. WHO READS AND WRITES SSceneParameters");

        emit("The scene path lives at a fixed address because ZLevelManager does.");
        emit("Writers are the menu choosing a mission. Readers are the loader.");
        emit("Reads matter most: one of them takes the path and does the work.");

        Map<String, Function> readers = new LinkedHashMap<>();
        Map<String, Function> writers = new LinkedHashMap<>();

        for (int i = 0; i < FIELDS.length; ++i)
        {
            Address address = toAddr(FIELDS[i]);

            emit("");
            emit("-- " + FIELD_NAMES[i] + " at " + address);

            Reference[] references = getReferencesTo(address);

            if (references == null || references.length == 0)
            {
                emit("   no references - Ghidra has not marked it as data.");
                emit("   Go to " + address + ", press D, then run this again.");

                continue;
            }

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());

                if (owner == null)
                {
                    continue;
                }

                final RefType type = reference.getReferenceType();
                final String  kind = type.isWrite() ? "writes" : type.isRead() ? "reads " : "uses  ";

                emit(String.format("   %s  %s", kind, describe(owner)));

                if (type.isWrite())
                {
                    writers.put(owner.getEntryPoint().toString(), owner);
                }
                else
                {
                    readers.put(owner.getEntryPoint().toString(), owner);
                }
            }
        }

        emit("");
        emit("Decompiling the readers first - the loader is among them:");

        int shown = 0;

        for (Function function : readers.values())
        {
            decompile(function, "reads the scene path");

            if (++shown >= 4) { emit(""); emit("(stopping after four readers)"); break; }
        }

        emit("");
        emit("And the writers, which is the menu handing a mission over:");

        shown = 0;

        for (Function function : writers.values())
        {
            decompile(function, "writes the scene path");

            if (++shown >= 3) { emit(""); emit("(stopping after three writers)"); break; }
        }
    }

    // ---- 2. the four the strings already named ------------------------------

    private void known()
    {
        heading("2. THE FUNCTIONS THAT NAME A SCENE");

        emit("From the string search. Going to the main menu is a level load, so");
        emit("one of these is the whole path with a different destination. The");
        emit("benchmark one is the same thing with nothing else attached.");

        for (long address : KNOWN)
        {
            Function function = getFunctionAt(toAddr(address));

            if (function == null)
            {
                function = getFunctionContaining(toAddr(address));
            }

            if (function == null)
            {
                emit("");
                emit(String.format("%08X - nothing there. Go to it, press D, then F.", address));

                continue;
            }

            emit("");
            emit("Calls:");

            int shown = 0;

            for (Function callee : function.getCalledFunctions(monitor))
            {
                emit("  " + describe(callee));

                if (++shown >= 25) { emit("  ..."); break; }
            }

            decompile(function, "names a scene");
        }
    }
}
