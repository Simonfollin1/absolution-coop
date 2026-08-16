#include "Memory/GameOffsets.h"

namespace
{
    // PE SizeOfImage for each known 1.0.447.0 build, taken from
    // LiveSplit.HitmanAbsolution's ExpectedDllSizes enum.
    constexpr uint32_t STEAM_IMAGE_SIZE = 36777984;
    constexpr uint32_t GOG_IMAGE_SIZE   = 35799040;

    constexpr GameOffsets::Table STEAM_TABLE = {
        /* isInLoadingScreen   */ 0xE53E20,
        /* isInMenu            */ 0xD61C7B,
        /* isResultScreen      */ 0xE213E8,
        /* currentLevel        */ 0xE20F48,
        /* currentSection      */ 0xD60F94,
        /* terminusElevatorPtr */ 0xE39520,
        /* finaleExplosion     */ { 0xD610B4, 5, { 0x8, 0x5FC, 0x10, 0x74, 0x508 } },
    };

    constexpr GameOffsets::Table GOG_TABLE = {
        /* isInLoadingScreen   */ 0xC96750,
        /* isInMenu            */ 0xCA983B,
        /* isResultScreen      */ 0xCA983D,
        /* currentLevel        */ 0xD68B08,
        /* currentSection      */ 0xCA8B74,
        /* terminusElevatorPtr */ 0xC939D0,
        // LiveSplit's GOG entry for this flag is a known placeholder (it reuses
        // the result-screen address and the author flagged it as unfinished),
        // so we treat it as unavailable rather than reporting a wrong value.
        /* finaleExplosion     */ { 0, 0, {} },
    };

    constexpr GameOffsets::Table EMPTY_TABLE = { 0, 0, 0, 0, 0, 0, { 0, 0, {} } };

    // Reads SizeOfImage straight out of the loaded module's PE headers, which
    // avoids taking a dependency on psapi just to call GetModuleInformation.
    uint32_t GetImageSize(uintptr_t base)
    {
        if (!base)
        {
            return 0;
        }

        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return 0;
        }

        const auto* ntHeaders =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);

        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            return 0;
        }

        return ntHeaders->OptionalHeader.SizeOfImage;
    }

    // Guards against obviously bogus pointers before we dereference them. The
    // game is 32-bit, so anything below the first 64 KiB or above the 2 GiB
    // user-space boundary is not a real heap pointer.
    bool LooksLikePointer(uintptr_t value)
    {
        return value >= 0x10000 && value < 0x7FFF0000;
    }

    // --- The probe cache -----------------------------------------------------
    //
    // VirtualQuery is a call into the kernel, and the reads below were making
    // eight to ten of them every frame: two for the Terminus elevator flag and
    // six for the finale chain, every frame, whether or not any overlay was on.
    // Nothing a player can switch off stopped it.
    //
    // What makes caching possible is that VirtualQuery does not answer about an
    // address, it answers about the region the address is in - BaseAddress and
    // RegionSize come back with every call and were being thrown away. The
    // pointers walked here land in the same few regions frame after frame: the
    // game's own image and a handful of heap segments.
    //
    // What makes it safe is that the fault it prevents was never the only
    // defence. Every read through these probes is inside __try, and that
    // handler stays. The probe is there so faulting is not routine - a vectored
    // handler from any mod in the process would write a report on every one -
    // and a cached answer that has gone stale costs one caught fault, not a
    // crash.
    //
    // Staleness is bounded two ways: an entry expires after a fixed number of
    // uses, and a level change drops the lot.

    constexpr int REGION_CACHE_SIZE = 8;

    // About four seconds at 60fps for a value read once a frame. Long enough
    // that the syscall all but disappears, short enough that a region freed by
    // something we did not notice comes back into question quickly.
    constexpr int REGION_MAX_USES = 240;

    struct CachedRegion
    {
        uintptr_t begin = 0;
        uintptr_t end   = 0;
        int       uses  = 0;
    };

    // Per thread. The game thread and the render thread both walk these
    // pointers, and a cache neither of them shares needs no lock, no atomics,
    // and cannot hand one thread the other's half-written entry.
    thread_local CachedRegion t_regions[REGION_CACHE_SIZE];
    thread_local int          t_regionCursor     = 0;
    thread_local unsigned int t_regionGeneration = 0;

    // Bumped on level change. Not atomic on purpose: a torn read of a counter
    // whose only use is inequality means one extra flush, which is the safe
    // direction and cheaper than an interlocked read per probe.
    volatile unsigned int g_probeGeneration = 0;

    // Counters for the diagnostics tab, so "the syscalls are gone" is something
    // to read rather than something to believe.
    unsigned int g_probeCalls = 0;
    unsigned int g_probeHits  = 0;

    void FlushRegionsIfStale()
    {
        const unsigned int generation = g_probeGeneration;

        if (t_regionGeneration == generation)
        {
            return;
        }

        for (CachedRegion& region : t_regions)
        {
            region = CachedRegion{};
        }

        t_regionGeneration = generation;
    }

    // Asks the kernel whether the bytes are actually there before reading them.
    //
    // Catching the fault afterwards is not good enough. A vectored exception
    // handler runs before any structured handler, ours writes a crash report,
    // and the other mod's handler does not know this fault was expected - each
    // DLL has its own copy of that state. Two mods loaded together would turn
    // every guarded read into two disk writes. Not faulting in the first place
    // is both cheaper and the only version that composes.
    bool IsReadable(uintptr_t address, size_t size)
    {
        if (!LooksLikePointer(address))
        {
            return false;
        }

        FlushRegionsIfStale();

        for (CachedRegion& region : t_regions)
        {
            if (region.end == 0 || address < region.begin || address + size > region.end)
            {
                continue;
            }

            if (++region.uses > REGION_MAX_USES)
            {
                region = CachedRegion{};

                break;
            }

            ++g_probeHits;

            return true;
        }

        ++g_probeCalls;

        MEMORY_BASIC_INFORMATION info;

        if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info))
        {
            return false;
        }

        if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) || info.Protect == PAGE_NOACCESS)
        {
            return false;
        }

        // The read must not run off the end of the region it started in.
        const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const uintptr_t regionEnd   = regionBegin + info.RegionSize;

        if (address + size > regionEnd)
        {
            return false;
        }

        // Only readable regions are remembered. A refusal is the answer that
        // has to stay live: it is how a pointer that has gone bad gets noticed,
        // and it is rare enough that re-asking costs nothing.
        CachedRegion& slot = t_regions[t_regionCursor];

        slot.begin = regionBegin;
        slot.end   = regionEnd;
        slot.uses  = 0;

        t_regionCursor = (t_regionCursor + 1) % REGION_CACHE_SIZE;

        return true;
    }
}

void GameOffsets::InvalidateProbeCache()
{
    ++g_probeGeneration;
}

unsigned int GameOffsets::GetProbeSyscalls()
{
    return g_probeCalls;
}

unsigned int GameOffsets::GetProbeCacheHits()
{
    return g_probeHits;
}

uintptr_t GameOffsets::GetModuleBase()
{
    // nullptr resolves the process' main module. This matches how the SDK
    // itself computes BaseAddress in SDK::InitializeSingletons().
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

    return base;
}

GameOffsets::Build GameOffsets::GetBuild()
{
    static Build build = []
    {
        switch (GetImageSize(GetModuleBase()))
        {
        case STEAM_IMAGE_SIZE: return Build::Steam;
        case GOG_IMAGE_SIZE:   return Build::GOG;
        default:               return Build::Unknown;
        }
    }();

    return build;
}

const char* GameOffsets::GetBuildName()
{
    switch (GetBuild())
    {
    case Build::Steam: return "Steam 1.0.447.0";
    case Build::GOG:   return "GOG 1.0.447.0";
    default:           return "Unknown build";
    }
}

bool GameOffsets::IsSupported()
{
    return GetBuild() != Build::Unknown;
}

const GameOffsets::Table& GameOffsets::GetTable()
{
    switch (GetBuild())
    {
    case Build::Steam: return STEAM_TABLE;
    case Build::GOG:   return GOG_TABLE;
    default:           return EMPTY_TABLE;
    }
}

namespace
{
    // Every dereference below is probed first, so in normal operation nothing
    // faults at all. The structured handler is a backstop for the race where a
    // page is unmapped between the probe and the read.
    bool WalkChain(const GameOffsets::PointerChain& chain, uintptr_t moduleBase, bool& valueOut)
    {
        uintptr_t address = moduleBase + chain.base;

        // Follow every link except the last; the final offset addresses the value.
        for (int i = 0; i < chain.count - 1; ++i)
        {
            if (!IsReadable(address, sizeof(uintptr_t)))
            {
                return false;
            }

            address = *reinterpret_cast<uintptr_t*>(address);

            if (!LooksLikePointer(address))
            {
                return false;
            }

            address += chain.offsets[i];
        }

        if (!IsReadable(address, sizeof(uintptr_t)))
        {
            return false;
        }

        address = *reinterpret_cast<uintptr_t*>(address);

        const uintptr_t field = address + chain.offsets[chain.count - 1];

        if (!IsReadable(field, sizeof(bool)))
        {
            return false;
        }

        valueOut = *reinterpret_cast<bool*>(field);

        return true;
    }

    bool ReadThroughPointer(uintptr_t pointerAddress, uintptr_t fieldOffset, bool& valueOut)
    {
        if (!IsReadable(pointerAddress, sizeof(uintptr_t)))
        {
            return false;
        }

        const uintptr_t target = *reinterpret_cast<uintptr_t*>(pointerAddress);
        const uintptr_t field  = target + fieldOffset;

        if (!IsReadable(field, sizeof(bool)))
        {
            return false;
        }

        valueOut = *reinterpret_cast<bool*>(field);

        return true;
    }

    // The __try lives here, in functions holding nothing that needs unwinding.
    bool GuardedWalkChain(const GameOffsets::PointerChain& chain, uintptr_t moduleBase, bool& valueOut)
    {
        __try
        {
            return WalkChain(chain, moduleBase, valueOut);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedReadThroughPointer(uintptr_t pointerAddress, uintptr_t fieldOffset, bool& valueOut)
    {
        __try
        {
            return ReadThroughPointer(pointerAddress, fieldOffset, valueOut);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool GameOffsets::ReadPointerBool(uintptr_t pointerOffset, uintptr_t fieldOffset, bool& valueOut)
{
    if (!pointerOffset || !IsSupported())
    {
        return false;
    }

    const uintptr_t moduleBase = GetModuleBase();

    if (!moduleBase)
    {
        return false;
    }

    return GuardedReadThroughPointer(moduleBase + pointerOffset, fieldOffset, valueOut);
}

bool GameOffsets::ReadChainBool(const PointerChain& chain, bool& valueOut)
{
    // These offsets come from a community autosplitter, not from anything we
    // verified. Probe before every link rather than trusting any of them.
    if (!chain.base || chain.count <= 0 || !IsSupported())
    {
        return false;
    }

    const uintptr_t moduleBase = GetModuleBase();

    if (!moduleBase)
    {
        return false;
    }

    return GuardedWalkChain(chain, moduleBase, valueOut);
}
