// Recovers vtables from MSVC RTTI in retail Hitman: Absolution 1.0.447.0.
//
// The first recon pass established that this binary carries RTTI, which the
// modding scene had assumed it did not. That makes vtable recovery exact
// rather than inferred: every vtable in an MSVC binary is preceded by a
// pointer to a CompleteObjectLocator, and that locator names its class.
//
// The layout, for 32-bit MSVC:
//
//   TypeDescriptor          { void* pVFTable; void* spare; char name[]; }
//     - the mangled name starts at offset +8, so a name string at S means
//       the descriptor begins at S-8.
//
//   CompleteObjectLocator   { DWORD signature; DWORD offset; DWORD cdOffset;
//                             DWORD pTypeDescriptor; DWORD pClassDescriptor; }
//     - pTypeDescriptor is at +0x0C, and signature is 0 on 32-bit.
//
//   vftable[-1]             points at the CompleteObjectLocator.
//
// So: name string -> descriptor -> locator -> vtable. Each step is a search
// for a 4-byte value, which is why this is fast enough to run over a 34 MB
// image without a plan.
//
// Writes hma_rtti.txt and hma_classes.txt to your home folder.
//
// IMPORTANT: create this as HmaRtti.java - a Ghidra Java script's class name
// must match its file name.
//
//@category Absolution

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.TreeMap;

public class HmaRtti extends GhidraScript
{
    private static final int ENTRIES_PER_TABLE = 24;

    // Classes whose vtables answer a question we are actually asking.
    private static final String[] TARGETS = {
        "ZHitman5",
        "ZActor",
        "ZHM5BaseCharacter",
        "ZHM5Health",
        "ZSharedSensorDef",
        "ZHM5LocomotionNetwork",
        "ZCheckPointManagerEntity",
    };

    private final List<String> lines   = new ArrayList<>();
    private final List<String> classes = new ArrayList<>();

    private Memory  memory;
    private Listing listing;

    // name -> address of the type descriptor
    private final TreeMap<String, Address> descriptors = new TreeMap<>();

    @Override
    public void run() throws Exception
    {
        memory  = currentProgram.getMemory();
        listing = currentProgram.getListing();

        emit("Hitman: Absolution RTTI vtable recovery");
        emit("program " + currentProgram.getName());

        collectTypeDescriptors();

        emit("");
        emit(descriptors.size() + " type descriptors found");

        for (String target : TARGETS)
        {
            try
            {
                recoverClass(target);
            }
            catch (Exception error)
            {
                emit("");
                emit("'" + target + "' failed: " + error);
            }
        }

        write(new File(System.getProperty("user.home"), "hma_rtti.txt"), lines);
        write(new File(System.getProperty("user.home"), "hma_classes.txt"), classes);

        println("");
        println("written to hma_rtti.txt and hma_classes.txt in " +
                System.getProperty("user.home"));
    }

    private void write(File file, List<String> content) throws Exception
    {
        try (PrintWriter writer = new PrintWriter(file, "UTF-8"))
        {
            for (String line : content)
            {
                writer.println(line);
            }
        }
    }

    private void emit(String text)
    {
        lines.add(text);
        println(text);
    }

    // ---- type descriptors -------------------------------------------------

    private void collectTypeDescriptors()
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

            if (!text.startsWith(".?A"))
            {
                continue;
            }

            classes.add(item.getAddress() + "  " + text);

            // .?AVZHitman5@@ -> ZHitman5. Strip the .?AV or .?AU prefix and
            // the @@ suffix; anything with a further @ is nested or templated
            // and is kept verbatim.
            String name = text.substring(4);

            if (name.endsWith("@@"))
            {
                name = name.substring(0, name.length() - 2);
            }

            descriptors.put(name, item.getAddress());
        }
    }

    // ---- vtable recovery --------------------------------------------------

    private Address findTypeDescriptor(String className)
    {
        Address direct = descriptors.get(className);

        if (direct != null)
        {
            return direct;
        }

        for (String key : descriptors.keySet())
        {
            if (key.equals(className) || key.startsWith(className + "@"))
            {
                return descriptors.get(key);
            }
        }

        return null;
    }

    /** Every address in initialised memory holding this 4-byte value. */
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
            if (!block.isInitialized() || block.isExecute())
            {
                continue;
            }

            Address at = block.getStart();

            while (at != null && !monitor.isCancelled())
            {
                at = memory.findBytes(at, block.getEnd(), pattern, null, true, monitor);

                if (at == null)
                {
                    break;
                }

                found.add(at);

                try { at = at.add(4); } catch (Exception e) { break; }
            }
        }

        return found;
    }

    private String describe(Address target)
    {
        Function function = getFunctionAt(target);

        if (function == null)
        {
            return String.format("%-40s %6s", "(no function)", "-");
        }

        return String.format("%-40s %6d", function.getName(),
                             function.getBody().getNumAddresses());
    }

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

        return (block == null || !block.isExecute()) ? null : target;
    }

    private void recoverClass(String className) throws Exception
    {
        emit("");
        emit("==========================================================================");
        emit(className);
        emit("==========================================================================");

        Address descriptor = findTypeDescriptor(className);

        if (descriptor == null)
        {
            emit("No type descriptor. Either the class is named differently in");
            emit("the binary, or it has no virtual functions and therefore no RTTI.");

            return;
        }

        // The mangled name sits at +8 inside the descriptor.
        Address descriptorStart = descriptor.subtract(8);

        emit("type descriptor at " + descriptorStart + " (name string at " + descriptor + ")");

        List<Address> references = findPointersTo(descriptorStart);

        emit(references.size() + " reference(s) to it");

        int recovered = 0;

        for (Address reference : references)
        {
            // pTypeDescriptor lives at +0x0C in a CompleteObjectLocator.
            Address locator;

            try { locator = reference.subtract(0x0C); } catch (Exception e) { continue; }

            int signature;

            try { signature = memory.getInt(locator); } catch (Exception e) { continue; }

            // 32-bit MSVC writes signature 0. Anything else is a coincidental
            // pointer rather than a locator.
            if (signature != 0)
            {
                continue;
            }

            int subObjectOffset;

            try { subObjectOffset = memory.getInt(locator.add(4)); } catch (Exception e) { continue; }

            List<Address> vtableSlots = findPointersTo(locator);

            for (Address slot : vtableSlots)
            {
                Address vtable;

                try { vtable = slot.add(4); } catch (Exception e) { continue; }

                if (codePointerAt(vtable) == null)
                {
                    continue;
                }

                recovered++;

                emit("");
                emit("--- vtable at " + vtable +
                     "   (sub-object offset 0x" + Integer.toHexString(subObjectOffset) + ") ---");

                for (int index = 0; index < ENTRIES_PER_TABLE; index++)
                {
                    Address entry  = vtable.add((long) index * 4);
                    Address target = codePointerAt(entry);

                    if (target == null)
                    {
                        emit(String.format("  [%2d] end of table", index));
                        break;
                    }

                    emit(String.format("  [%2d] %s  %s", index, target, describe(target)));
                }
            }
        }

        if (recovered == 0)
        {
            emit("");
            emit("Found the descriptor but no vtable pointing at it. The class");
            emit("may only appear as a base in other classes' locators.");
        }
        else
        {
            emit("");
            emit(recovered + " vtable(s) recovered.");
            emit("");
            emit("For ZHitman5 and ZActor: the sub-object offset tells the bases");
            emit("apart. The IBaseCharacter one is the table whose first five");
            emit("entries are one-line accessors - destructor, GetVariantRef,");
            emit("AddRef, Release, QueryInterface - followed by something");
            emit("substantial. That substantial entry is YouGotHit.");
        }
    }
}
