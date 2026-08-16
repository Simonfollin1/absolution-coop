// How the game actually starts a level.
//
// Writing the scene name into SSceneParameters and calling
// ZEntitySceneContext::CreateScene was tried in the game. It does not crash and
// it does not load: the scene name changes, the chapter does not, and the
// player stays exactly where they were.
//
// The reason is in the interface. Beside CreateScene sits
//
//     SetSceneResources(TResourcePtr<IEntityFactory>,
//                       TResourcePtr<IEntityBlueprintFactory>,
//                       ZResourcePtr)
//
// so CreateScene only instantiates whatever resources have already been set.
// Something else reads the scene name, loads that resource, hands it to
// SetSceneResources, and only then calls CreateScene. That something is what
// this script is looking for - it is the function the main menu runs when you
// pick a mission, and calling it is the whole of "follow me into this level".
//
// Anchors, both from the SDK's own hook table:
//
//     ZEntitySceneContext::CreateScene   0x004479E0
//     ZEntitySceneContext::ClearScene    0x00265A80
//     ZEngineAppCommon::ResetSceneCallback 0x0053D390
//
// Create as HmaSceneLoad.java. Writes hma_sceneload.txt to your home folder.
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
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class HmaSceneLoad extends GhidraScript
{
    private static final long CREATE_SCENE = 0x004479E0L;
    private static final long CLEAR_SCENE  = 0x00265A80L;
    private static final long RESET_SCENE  = 0x0053D390L;

    private final List<String> lines = new ArrayList<>();

    private DecompInterface decompiler;
    private Listing listing;

    @Override
    public void run() throws Exception
    {
        listing = currentProgram.getListing();

        emit("Hitman: Absolution - how a level is actually started");
        emit("program " + currentProgram.getName());
        emit("image base " + currentProgram.getImageBase());

        decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        try { callers(); }     catch (Exception e) { emit("callers failed: " + e); }
        try { sceneStrings(); } catch (Exception e) { emit("strings failed: " + e); }

        decompiler.dispose();

        File output = new File(System.getProperty("user.home"), "hma_sceneload.txt");

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

    private void decompile(Function function)
    {
        if (function == null) return;

        emit("");
        emit("--- " + function.getName() + " at " + function.getEntryPoint());
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

    // ---- who calls the scene functions -------------------------------------

    private void callers() throws Exception
    {
        heading("1. WHO CALLS CreateScene, ClearScene AND ResetSceneCallback");

        emit("A real transition should call more than one of these, in order,");
        emit("with a resource load in between. Anything appearing under two of");
        emit("them is the transition driver.");

        final long[] anchors  = { CREATE_SCENE, CLEAR_SCENE, RESET_SCENE };
        final String[] names  = { "CreateScene", "ClearScene", "ResetSceneCallback" };

        Set<String> seenEverywhere = new LinkedHashSet<>();

        for (int i = 0; i < anchors.length; ++i)
        {
            emit("");
            emit("-- " + names[i] + " at " + toAddr(anchors[i]));

            Function target = getFunctionAt(toAddr(anchors[i]));

            if (target == null)
            {
                target = getFunctionContaining(toAddr(anchors[i]));
            }

            if (target == null)
            {
                emit("   nothing there - disassemble it and run again");
                continue;
            }

            Set<Function> callers = target.getCallingFunctions(monitor);

            if (callers.isEmpty())
            {
                emit("   no direct callers - it is virtual, reached through a vtable");
            }

            for (Function caller : callers)
            {
                emit("   " + describe(caller));
                seenEverywhere.add(caller.getEntryPoint().toString());
            }
        }

        emit("");
        emit("Decompiling the first few, which is where the missing steps are:");

        int shown = 0;

        for (String entry : seenEverywhere)
        {
            decompile(getFunctionAt(toAddr(Long.parseLong(entry, 16))));

            if (++shown >= 4) { emit(""); emit("(stopping after four)"); break; }
        }
    }

    // ---- the scene paths themselves ----------------------------------------

    private void sceneStrings()
    {
        heading("2. WHO TOUCHES THE SCENE PATHS");

        emit("Every level is assembly:/Scenes/<name>/<name>_Main.entity, and the");
        emit("menu boots from assembly:/scenes/Menu/Menu_Main.entity in HMA.ini.");
        emit("Whatever reads or builds one of those strings is on the road in.");
        emit("");

        String[] needles = {
            "assembly:/Scenes/",
            "assembly:/scenes/",
            "_Main.entity",
            ".entity",
        };

        Iterator<Data> data = listing.getDefinedData(true);

        int shown = 0;

        while (data.hasNext() && !monitor.isCancelled() && shown < 60)
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

            boolean matched = false;

            for (String needle : needles)
            {
                if (text.contains(needle)) { matched = true; break; }
            }

            if (!matched) continue;

            Reference[] references = getReferencesTo(item.getAddress());

            if (references == null || references.length == 0)
            {
                continue;
            }

            emit(String.format("  \"%s\" at %s", text, item.getAddress()));

            Set<String> here = new LinkedHashSet<>();

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());
                if (owner == null) continue;

                if (here.add(owner.getEntryPoint().toString()))
                {
                    emit("      <- " + describe(owner));
                }
            }

            ++shown;
        }

        emit("");
        emit("By hand from here: the one that takes a scene path and ends up at");
        emit("CreateScene is the function to call. Its arguments are what the");
        emit("mod needs to fill in.");
    }
}
