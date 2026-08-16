#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Coop::Game
{
    class ConfigVars;
    class WorldProbe;

    // One vtable found hanging off a live object, named by its own RTTI.
    //
    // MSVC puts a pointer to the complete object locator immediately before
    // the first entry of every vtable it emits, and the locator points at a
    // type descriptor whose name is a decorated string in the image. So a
    // running game can tell us what its objects actually are - no headers, no
    // guessing, no disassembler.
    struct VtableReport
    {
        // Offset of the vtable pointer within the object.
        size_t objectOffset = 0;

        // Both as offsets into HMA.exe, which is what a disassembler wants.
        uintptr_t vtableRva = 0;

        // ".?AVZHitman5@@" and friends, or empty when there is no locator -
        // which is itself worth knowing, since a class compiled without RTTI
        // cannot be identified this way.
        std::string decoratedName;

        // Entries up to where the table stops looking like code, each named by
        // the module it is in. Not bare offsets: the first dump taken in the
        // game printed our own replacement entry as a game address, because
        // everything was being reported relative to HMA.exe whether it belonged
        // to it or not.
        std::vector<std::string> entries;
    };

    // Every vtable pointer in the first `searchBytes` of an object.
    //
    // Scanning rather than using the offsets from the headers, because the
    // headers are for the 2012 development build and this is retail - and
    // because a scan finds the bases nobody wrote down.
    std::vector<VtableReport> ScanVtables(const void* object, size_t searchBytes = 0x40);

    // Writes everything the mod knows to a text file next to HMA.exe and
    // returns the path it wrote, or an empty string with the reason in `error`.
    //
    // The point of a file rather than a panel: it can be pasted somewhere
    // useful. A panel can only be squinted at.
    std::string WriteDump(const ConfigVars& configVars,
                          const WorldProbe& probe,
                          const std::string& bindingExpression,
                          const std::vector<std::string>& log,
                          std::string& error);
}
