// The level loader, third attempt, with the two earlier mistakes corrected.
//
// The first pass asked for callers of CreateScene and got a destructor, because
// Ghidra has no function at that address and returns whatever surrounds it.
//
// The second asked for references to 0x01221314, the scene path inside
// SSceneParameters, and got none. That was the wrong question. ZLevelManager is
// a static object at 0x01221310 and the parameters are at +0x4, and a compiler
// never emits an absolute load of a field - it puts the object's base in a
// register and reaches the field through it. Every reference is on the base.
//
// So this one asks four things instead, none of which depend on Ghidra having
// got a function boundary right:
//
//   1. Who touches ZLevelManager at 0x01221310, read or write.
//   2. What the helpers around the scene path actually are. FUN_007f2630 is
//      handed the path in all four functions that name a scene, but it also
//      appears in teardown loops, so it is either the loader or ZString's
//      assignment operator. Caller count settles it: a string primitive has
//      thousands, a scene setter has a handful.
//   3. Every named message the game registers a handler for. FUN_0070ca40
//      registers "Start", "Stop" and "LoadingTransitionComplete" by name, so
//      there is a message bus, and whatever the menu sends to begin a mission
//      has a name. Sending that message beats rebuilding the loader.
//   4. CreateScene and ClearScene, disassembled into existence first so there
//      is something to decompile.
//
// Create as HmaSceneLoad3.java. Writes hma_sceneload3.txt to your home folder.
// It is slower than the others - it searches the whole image twice.
//
//@category Absolution

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class HmaSceneLoad3 extends GhidraScript
{
    // The object itself, not a field of it.
    private static final long LEVEL_MANAGER = 0x01221310L;
    private static final long LEVEL_MANAGER_SIZE = 0x140L;

    // The helpers the four scene-naming functions are built out of, and what
    // the evidence so far says each one is. The caller count is the test: the
    // ones that turn out to be string primitives get dropped, and whatever is
    // left holding a scene path is the answer.
    private static final long[] HELPERS = {
        0x007f2630L,   // handed the scene path in all four - loader, or operator=
        0x0080a050L,   // ZString from a C literal
        0x00820460L,   // ZString destructor
        0x00536650L,   // ZString from something else
        0x005963f0L,   // split by char - proven with ';' ' ' and '/'
        0x009c04d0L,   // compare or find
        0x005e7310L,   // registers a named message handler
    };

    private static final String[] HELPER_NOTES = {
        "handed the scene path in all four - the candidate",
        "ZString from a C literal",
        "ZString destructor",
        "ZString from something else",
        "split by character",
        "compare or find",
        "registers a named message handler",
    };

    // Where the SDK says these live. Ghidra has no function at either, so both
    // get disassembled into existence before anything is asked of them.
    private static final long CREATE_SCENE = 0x004479e0L;
    private static final long CLEAR_SCENE  = 0x00265a80L;

    // Anything with more callers than this is plumbing, not a scene loader.
    private static final int PLUMBING = 200;

    private final List<String> lines = new ArrayList<>();

    private DecompInterface decompiler;

    @Override
    public void run() throws Exception
    {
        emit("Hitman: Absolution - the level loader, third attempt");
        emit("program " + currentProgram.getName());
        emit("image base " + currentProgram.getImageBase());

        decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        try { levelManager(); } catch (Exception e) { emit("level manager failed: " + e); }
        try { helpers(); }      catch (Exception e) { emit("helpers failed: " + e); }
        try { messages(); }     catch (Exception e) { emit("messages failed: " + e); }
        try { sceneContext(); } catch (Exception e) { emit("scene context failed: " + e); }

        decompiler.dispose();

        File output = new File(System.getProperty("user.home"), "hma_sceneload3.txt");

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

    private int callerCount(Function function)
    {
        try
        {
            return function.getCallingFunctions(monitor).size();
        }
        catch (Exception e)
        {
            return -1;
        }
    }

    private void decompile(Function function, String why)
    {
        if (function == null) return;

        emit("");
        emit("--- " + why + ": " + function.getName() + " at " + function.getEntryPoint());
        emit("");

        try
        {
            DecompileResults results = decompiler.decompileFunction(function, 180, monitor);

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

    // The string a reference points at, if it points at one.
    private String stringAt(Address address)
    {
        if (address == null) return null;

        Data data = getDataAt(address);

        if (data == null) return null;

        StringDataInstance instance = StringDataInstance.getStringDataInstance(data);

        if (instance == null || instance == StringDataInstance.NULL_INSTANCE) return null;

        String value = instance.getStringValue();

        return (value == null || value.isEmpty()) ? null : value;
    }

    // Every string literal a function mentions, in order.
    private List<String> stringsIn(Function function)
    {
        List<String> found = new ArrayList<>();

        if (function == null) return found;

        ReferenceIterator iterator =
            currentProgram.getReferenceManager().getReferenceIterator(function.getEntryPoint());

        while (iterator.hasNext() && !monitor.isCancelled())
        {
            Reference reference = iterator.next();

            if (!function.getBody().contains(reference.getFromAddress()))
            {
                break;
            }

            String text = stringAt(reference.getToAddress());

            if (text != null && !found.contains(text))
            {
                found.add(text);

                if (found.size() >= 14) break;
            }
        }

        return found;
    }

    // ---- 1. ZLevelManager, at the object rather than at a field -------------

    private void levelManager() throws Exception
    {
        heading("1. WHO TOUCHES ZLevelManager");

        emit("The object is at " + toAddr(LEVEL_MANAGER) + " and is 0x140 bytes.");
        emit("SSceneParameters is the first thing in it, at +0x4. Whoever writes");
        emit("the scene path is the menu choosing a mission; whoever reads it is");
        emit("on the way into the load.");

        Map<String, Function> readers = new LinkedHashMap<>();
        Map<String, Function> writers = new LinkedHashMap<>();

        for (long offset = 0; offset < LEVEL_MANAGER_SIZE; offset += 4)
        {
            Address address = toAddr(LEVEL_MANAGER + offset);

            Reference[] references = getReferencesTo(address);

            if (references == null || references.length == 0)
            {
                continue;
            }

            emit("");
            emit(String.format("-- %s  (ZLevelManager + 0x%X)", address, offset));

            for (Reference reference : references)
            {
                Function owner = getFunctionContaining(reference.getFromAddress());

                if (owner == null) continue;

                RefType type = reference.getReferenceType();
                String  kind = type.isWrite() ? "writes" : type.isRead() ? "reads " : "uses  ";

                emit(String.format("   %s  %s", kind, describe(owner)));

                if (type.isWrite()) writers.put(owner.getEntryPoint().toString(), owner);
                else                readers.put(owner.getEntryPoint().toString(), owner);
            }
        }

        // Ghidra only records a reference when its analysis recognised the
        // operand. The constant itself is in the instruction stream either way,
        // so search the bytes for it as well - that finds the ones the analyser
        // missed, which is the whole reason the last attempt came back empty.
        emit("");
        emit("Searching the image for the constant itself, which does not depend");
        emit("on the analyser having recognised anything:");

        for (long target : new long[] { LEVEL_MANAGER, LEVEL_MANAGER + 4 })
        {
            emit("");
            emit(String.format("-- little-endian %08X", target));

            String pattern = String.format("\\x%02x\\x%02x\\x%02x\\x%02x",
                target & 0xFF, (target >> 8) & 0xFF, (target >> 16) & 0xFF, (target >> 24) & 0xFF);

            Address[] hits = findBytes(currentProgram.getMinAddress(), pattern, 400);

            if (hits == null || hits.length == 0)
            {
                emit("   nothing - which would mean the address is wrong.");
                continue;
            }

            Set<String> seen = new LinkedHashSet<>();

            for (Address hit : hits)
            {
                Function owner = getFunctionContaining(hit);

                if (owner == null) continue;

                if (seen.add(owner.getEntryPoint().toString()))
                {
                    emit("   " + describe(owner));

                    readers.put(owner.getEntryPoint().toString(), owner);
                }
            }

            emit(String.format("   %d occurrences, %d distinct functions", hits.length, seen.size()));
        }

        emit("");
        emit("Decompiling the writers - the menu handing a mission over:");

        int shown = 0;

        for (Function function : writers.values())
        {
            decompile(function, "writes into ZLevelManager");

            if (++shown >= 4) { emit(""); emit("(stopping after four writers)"); break; }
        }

        emit("");
        emit("And the ones that only read, smallest first, since the loader is");
        emit("more likely to be a focused function than a large one:");

        shown = 0;

        for (Function function : readers.values())
        {
            if (writers.containsKey(function.getEntryPoint().toString())) continue;

            emit("");
            emit("  " + describe(function) + "   " + callerCount(function) + " callers");

            if (++shown >= 40) { emit("  ..."); break; }
        }
    }

    // ---- 2. what the helpers actually are ----------------------------------

    private void helpers()
    {
        heading("2. THE HELPERS AROUND THE SCENE PATH");

        emit("Caller count is the test. A ZString primitive is called from");
        emit("everywhere; something that loads a level is not.");

        List<Function> worthReading = new ArrayList<>();

        for (int i = 0; i < HELPERS.length; ++i)
        {
            Address address = toAddr(HELPERS[i]);

            Function function = getFunctionAt(address);

            if (function == null) function = getFunctionContaining(address);

            emit("");

            if (function == null)
            {
                emit(String.format("%08X  no function here", HELPERS[i]));
                emit("          " + HELPER_NOTES[i]);
                continue;
            }

            int callers = callerCount(function);

            emit(String.format("%s  %d callers", describe(function), callers));
            emit("          " + HELPER_NOTES[i]);

            if (callers >= 0 && callers <= PLUMBING)
            {
                emit("          few enough to be worth reading");
                worthReading.add(function);
            }
            else
            {
                emit("          plumbing - called from everywhere, not the loader");
            }
        }

        for (Function function : worthReading)
        {
            decompile(function, "not plumbing");
        }
    }

    // ---- 3. the named message bus ------------------------------------------

    private void messages()
    {
        heading("3. EVERY NAMED MESSAGE HANDLER");

        emit("FUN_005e7310 takes a name and a delegate. Listing its callers with");
        emit("the strings each one mentions gives the whole vocabulary of the");
        emit("message bus - and the message that starts a mission is in it.");

        Function registrar = getFunctionAt(toAddr(0x005e7310L));

        if (registrar == null)
        {
            emit("");
            emit("no function at 005e7310 - go to it, press D then F, and re-run.");
            return;
        }

        Set<Function> callers;

        try
        {
            callers = registrar.getCallingFunctions(monitor);
        }
        catch (Exception e)
        {
            emit("could not list callers: " + e);
            return;
        }

        emit("");
        emit(callers.size() + " functions register a handler.");

        int shown = 0;

        for (Function caller : callers)
        {
            List<String> strings = stringsIn(caller);

            if (strings.isEmpty()) continue;

            emit("");
            emit("  " + describe(caller));

            for (String text : strings)
            {
                emit("      \"" + text + "\"");
            }

            if (++shown >= 60) { emit(""); emit("(stopping after sixty)"); break; }
        }
    }

    // ---- 4. CreateScene and ClearScene, made real first ---------------------

    private void sceneContext() throws Exception
    {
        heading("4. CreateScene AND ClearScene");

        emit("The SDK has no method on ZEntitySceneContext that takes a scene");
        emit("path - SetSceneResources puts resources in and CreateScene");
        emit("instantiates what is already there. So CreateScene is not the");
        emit("answer, but reading it says what it expects to have been given,");
        emit("and that names the step that is missing.");

        for (long target : new long[] { CREATE_SCENE, CLEAR_SCENE })
        {
            Address address = toAddr(target);

            Function function = getFunctionAt(address);

            if (function == null)
            {
                emit("");
                emit(String.format("%08X has no function - disassembling it into existence", target));

                try
                {
                    disassemble(address);

                    function = createFunction(address, null);
                }
                catch (Exception e)
                {
                    emit("   failed: " + e);
                }
            }

            if (function == null)
            {
                emit("   still nothing. Go to " + address + ", press D, then F, then re-run.");
                continue;
            }

            emit("");
            emit("Calls:");

            int shown = 0;

            for (Function callee : function.getCalledFunctions(monitor))
            {
                emit("  " + describe(callee));

                if (++shown >= 30) { emit("  ..."); break; }
            }

            decompile(function, "scene context");
        }
    }
}
