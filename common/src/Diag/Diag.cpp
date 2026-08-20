#include "Diag/Diag.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <Windows.h>

// For the Visual C++ runtime version check below.
#pragma comment(lib, "version.lib")

namespace
{
    // ---- State ----

    CRITICAL_SECTION g_lock;
    bool  g_initialized = false;
    FILE* g_logFile     = nullptr;

    char g_modName[64]      = "mod";
    char g_logPath[MAX_PATH]   = {};
    char g_crashPath[MAX_PATH] = {};

    PVOID g_handler      = nullptr;
    LONG  g_reportsLeft  = 4;

    // Where each thread currently is. The crash handler reads the crashing
    // thread's own value, which is exactly the one we want.
    thread_local const char* t_stage = "not started";
    thread_local const char* t_role  = nullptr;

    // A short history across all threads, so a crash on a thread we never
    // instrumented still shows what the others were doing.
    constexpr int TRAIL_SIZE = 32;

    struct TrailEntry
    {
        DWORD       threadId;
        const char* stage;
    };

    TrailEntry g_trail[TRAIL_SIZE] = {};
    LONG       g_trailIndex        = 0;

    // Reused by the crash handler so a stack-overflow report does not need a
    // large stack buffer of its own.
    char g_reportBuffer[16384];

    // Set when the Visual C++ runtime is too old to run what we built.
    char g_runtimeWarning[256] = {};

    // Module table captured up front.
    //
    // The crash handler must not call GetModuleFileName: that takes the loader
    // lock, and a first-chance exception can perfectly well arrive while some
    // other thread is inside LoadLibrary holding it. The handler would then wait
    // for a lock that is waiting for us, and the game hangs at startup with no
    // window, no crash and nothing to read.
    struct ModuleEntry
    {
        uintptr_t base = 0;
        uintptr_t size = 0;
        char      name[64] = {};
    };

    constexpr int MAX_MODULES = 256;

    ModuleEntry g_modules[MAX_MODULES];
    volatile LONG g_moduleCount = 0;

    // Re-entrancy guard: if the handler itself faults we must not recurse.
    thread_local bool t_inHandler = false;

    // ---- Helpers ----

    void CurrentTime(char* out, size_t size)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);

        std::snprintf(out, size, "%02d:%02d:%02d.%03d",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    }

    // Walks the address space and records every mapped image. Called on normal
    // execution paths only, never from the handler.
    void CaptureModules()
    {
        LONG count = 0;
        uintptr_t address = 0;
        MEMORY_BASIC_INFORMATION info;
        HMODULE last = nullptr;

        while (count < MAX_MODULES &&
               VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == sizeof(info))
        {
            if (info.Type == MEM_IMAGE && info.AllocationBase && info.AllocationBase != last)
            {
                last = static_cast<HMODULE>(info.AllocationBase);

                char path[MAX_PATH] = {};

                if (GetModuleFileNameA(last, path, MAX_PATH))
                {
                    const char* name = std::strrchr(path, '\\');
                    name = name ? name + 1 : path;

                    ModuleEntry& entry = g_modules[count];
                    entry.base = reinterpret_cast<uintptr_t>(last);
                    entry.size = 0;   // filled below, once we know where it ends
                    std::snprintf(entry.name, sizeof(entry.name), "%s", name);

                    ++count;
                }
            }

            // Extend the most recent entry to cover this region.
            if (count > 0 && info.AllocationBase == last)
            {
                const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
                ModuleEntry& entry = g_modules[count - 1];

                if (end > entry.base)
                {
                    entry.size = end - entry.base;
                }
            }

            const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;

            if (next <= address)
            {
                break;
            }

            address = next;
        }

        InterlockedExchange(&g_moduleCount, count);
    }

    // Resolves an address against the captured table. No loader-lock calls, no
    // allocation - safe from inside an exception handler.
    void DescribeAddress(void* address, char* out, size_t size)
    {
        const auto target = reinterpret_cast<uintptr_t>(address);
        const LONG count = g_moduleCount;

        for (LONG i = 0; i < count; ++i)
        {
            const ModuleEntry& entry = g_modules[i];

            if (entry.size && target >= entry.base && target < entry.base + entry.size)
            {
                std::snprintf(out, size, "%s+0x%X",
                    entry.name, static_cast<unsigned int>(target - entry.base));
                return;
            }
        }

        std::snprintf(out, size, "0x%08X (not in a known module)",
            static_cast<unsigned int>(target));
    }

    // True when the address falls inside a module we captured, which is what
    // makes a raw stack scan readable.
    bool IsInAnyModule(uintptr_t value)
    {
        const LONG count = g_moduleCount;

        for (LONG i = 0; i < count; ++i)
        {
            const ModuleEntry& entry = g_modules[i];

            if (entry.size && value >= entry.base && value < entry.base + entry.size)
            {
                return true;
            }
        }

        return false;
    }

    // How many bytes are safely readable from addr, capped at want. The crash
    // handler dereferences a faulting register to show what it pointed at, and
    // that pointer is by definition suspect - so this asks the OS first and
    // never itself faults. Stays inside the one committed region VirtualQuery
    // reports, so a read that starts valid can never walk into an unmapped page.
    size_t ReadableBytes(uintptr_t addr, size_t want)
    {
        if (addr == 0)
        {
            return 0;
        }

        MEMORY_BASIC_INFORMATION info{};

        if (VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info)) != sizeof(info))
        {
            return 0;
        }

        if (info.State != MEM_COMMIT)
        {
            return 0;
        }

        constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                                 | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                 | PAGE_EXECUTE_WRITECOPY;

        if ((info.Protect & readable) == 0 || (info.Protect & PAGE_GUARD) != 0)
        {
            return 0;
        }

        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        const size_t avail = regionEnd - addr;

        return avail < want ? avail : want;
    }

    void WriteReport(const char* text)
    {
        const HANDLE file = CreateFileA(g_crashPath, FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }

    void LogLoadedModules()
    {
        // Written once at load time rather than from the crash handler, where
        // walking the loader's structures could deadlock.
        if (!g_logFile)
        {
            return;
        }

        std::fprintf(g_logFile, "  loaded modules:\n");

        // Walk the address space instead of the module list: same information,
        // no dependency on psapi or toolhelp.
        uintptr_t address = 0;
        MEMORY_BASIC_INFORMATION info;
        HMODULE lastReported = nullptr;

        while (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == sizeof(info))
        {
            if (info.Type == MEM_IMAGE && info.AllocationBase != lastReported)
            {
                lastReported = static_cast<HMODULE>(info.AllocationBase);

                char path[MAX_PATH] = {};

                if (GetModuleFileNameA(lastReported, path, MAX_PATH))
                {
                    const char* name = std::strrchr(path, '\\');
                    name = name ? name + 1 : path;

                    std::fprintf(g_logFile, "    %08X  %s\n",
                        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(lastReported)), name);
                }
            }

            const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;

            if (next <= address)
            {
                break;
            }

            address = next;
        }

        std::fflush(g_logFile);
    }

    bool IsFatal(DWORD code)
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_PRIV_INSTRUCTION:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_STACK_OVERFLOW:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                return true;
            default:
                return false;
        }
    }

    // Appends to a fixed buffer and never runs off the end. A free function
    // rather than a lambda because it takes varargs.
    void Append(char* buffer, size_t capacity, size_t& used, const char* format, ...)
    {
        if (used + 1 >= capacity)
        {
            return;
        }

        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(buffer + used, capacity - used, format, arguments);
        va_end(arguments);

        if (written > 0)
        {
            used += static_cast<size_t>(written);

            if (used >= capacity)
            {
                used = capacity - 1;
            }
        }
    }

    LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
    {
        if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;

        if (!IsFatal(record.ExceptionCode))
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // If the handler itself faults, do not come back round.
        if (t_inHandler)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Cap the number of reports. A first-chance access violation is not
        // always fatal, and we must not turn one into a log flood.
        if (InterlockedDecrement(&g_reportsLeft) < 0)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        t_inHandler = true;

        const CONTEXT& context = *pointers->ContextRecord;

        char* buffer = g_reportBuffer;
        constexpr size_t capacity = sizeof(g_reportBuffer);
        size_t used = 0;

#define append(...) Append(buffer, capacity, used, __VA_ARGS__)

        char timeText[32];
        CurrentTime(timeText, sizeof(timeText));

        char where[MAX_PATH + 32];
        DescribeAddress(record.ExceptionAddress, where, sizeof(where));

        append("\n==== %s crash report ====\n", g_modName);
        append("time            %s\n", timeText);
        append("thread          %lu (%s)\n", GetCurrentThreadId(),
               t_role ? t_role : "unnamed");
        append("exception       0x%08X\n", record.ExceptionCode);
        append("at              %s\n", where);

        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            const ULONG_PTR operation = record.ExceptionInformation[0];
            const ULONG_PTR address   = record.ExceptionInformation[1];

            append("access          %s 0x%08X\n",
                operation == 0 ? "read from" : (operation == 1 ? "write to" : "execute at"),
                static_cast<unsigned int>(address));
        }

        append("last stage      %s\n", t_stage ? t_stage : "(none)");

        append("\nrecent stages (newest first)\n");

        const LONG trailIndex = g_trailIndex;

        for (int i = 1; i <= TRAIL_SIZE; ++i)
        {
            const TrailEntry& entry = g_trail[((trailIndex - i) % TRAIL_SIZE + TRAIL_SIZE) % TRAIL_SIZE];

            if (entry.stage)
            {
                append("  [tid %5lu] %s\n", entry.threadId, entry.stage);
            }
        }

#ifdef _M_IX86
        append("\nregisters\n");
        append("  eip %08X  esp %08X  ebp %08X\n", context.Eip, context.Esp, context.Ebp);
        append("  eax %08X  ebx %08X  ecx %08X  edx %08X\n", context.Eax, context.Ebx, context.Ecx, context.Edx);
        append("  esi %08X  edi %08X\n", context.Esi, context.Edi);

        // What the registers point at. A fault inside a container or a
        // refcounted structure names itself here: a released ZString atom, for
        // instance, shows its length and refcount in the first bytes and its
        // characters in the ascii column, so the string being freed is legible
        // straight from the report without a full-memory dump. Guarded, so a
        // wild register value just reads "<unreadable>" instead of re-faulting.
        {
            append("\nmemory at registers (guarded, up to 64 bytes: hex | ascii)\n");

            const struct { const char* name; uintptr_t value; } probes[] = {
                { "eax", context.Eax }, { "ebx", context.Ebx },
                { "ecx", context.Ecx }, { "edx", context.Edx },
                { "esi", context.Esi }, { "edi", context.Edi },
            };

            for (const auto& probe : probes)
            {
                const size_t count = ReadableBytes(probe.value, 64);

                if (count == 0)
                {
                    append("  %s %08X  <unreadable>\n",
                        probe.name, static_cast<unsigned int>(probe.value));

                    continue;
                }

                const uint8_t* bytes = reinterpret_cast<const uint8_t*>(probe.value);

                char hex[3 * 64 + 1];
                char ascii[64 + 1];
                size_t hexUsed = 0;

                for (size_t i = 0; i < count; ++i)
                {
                    std::snprintf(hex + hexUsed, sizeof(hex) - hexUsed, "%02X ", bytes[i]);
                    hexUsed += 3;
                    ascii[i] = (bytes[i] >= 32 && bytes[i] < 127)
                                   ? static_cast<char>(bytes[i])
                                   : '.';
                }

                ascii[count] = '\0';

                append("  %s %08X  %s| %s\n",
                    probe.name, static_cast<unsigned int>(probe.value), hex, ascii);
            }
        }

        append("\nstack (addresses that land inside a module)\n");

        // Bound the scan by the committed stack region rather than trusting
        // esp: on a stack overflow the pointer is exactly what is broken.
        MEMORY_BASIC_INFORMATION stackInfo;

        if (VirtualQuery(reinterpret_cast<void*>(context.Esp), &stackInfo, sizeof(stackInfo)) == sizeof(stackInfo) &&
            stackInfo.State == MEM_COMMIT)
        {
            const uintptr_t limit = reinterpret_cast<uintptr_t>(stackInfo.BaseAddress) + stackInfo.RegionSize;

            int printed = 0;

            for (uintptr_t slot = context.Esp;
                 slot + sizeof(uintptr_t) <= limit && printed < 40 && used + 256 < capacity;
                 slot += sizeof(uintptr_t))
            {
                const uintptr_t value = *reinterpret_cast<const uintptr_t*>(slot);

                if (!IsInAnyModule(value))
                {
                    continue;
                }

                char described[MAX_PATH + 32];
                DescribeAddress(reinterpret_cast<void*>(value), described, sizeof(described));

                append("  +0x%04X  %08X  %s\n",
                    static_cast<unsigned int>(slot - context.Esp),
                    static_cast<unsigned int>(value), described);

                ++printed;
            }
        }
#endif

        append("==== end of report ====\n\n");

#undef append

        WriteReport(buffer);

        t_inHandler = false;

        // Let the game's own handler run: it writes the minidump, and a report
        // from us is worth nothing if it costs the dump.
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

namespace Diag
{
    void Initialize(const char* modName)
    {
        if (g_initialized)
        {
            return;
        }

        InitializeCriticalSection(&g_lock);

        std::snprintf(g_modName, sizeof(g_modName), "%s", modName ? modName : "mod");
        std::snprintf(g_logPath, sizeof(g_logPath), "mods\\%s.log", g_modName);
        std::snprintf(g_crashPath, sizeof(g_crashPath), "mods\\%s-crash.log", g_modName);

        // Keep the previous launch before truncating this one.
        //
        // Opening "w" on its own was right for the common case and exactly wrong
        // for the failure that matters: the game crashes on startup once or
        // twice and then launches, and the launch that worked overwrites the log
        // of the one that did not. The evidence is destroyed by the act of
        // getting past the bug. One generation back is enough - after "it
        // crashed twice then started", the second crash is sitting in .prev.log.
        char previousPath[MAX_PATH] = {};
        std::snprintf(previousPath, sizeof(previousPath), "mods\\%s.prev.log", g_modName);

        // MoveFileEx rather than rename: it replaces an existing target, and it
        // failing is not a reason to skip the log we came here to open.
        MoveFileExA(g_logPath, previousPath, MOVEFILE_REPLACE_EXISTING);

        g_logFile = std::fopen(g_logPath, "w");
        g_initialized = true;

        if (!g_logFile)
        {
            return;
        }

        char timeText[32];
        CurrentTime(timeText, sizeof(timeText));

        SYSTEMTIME today;
        GetLocalTime(&today);

        std::fprintf(g_logFile, "%s log - %04d-%02d-%02d %s\n",
            g_modName, today.wYear, today.wMonth, today.wDay, timeText);

        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        std::fprintf(g_logFile, "  executable   %s\n", exePath);
        std::fprintf(g_logFile, "  process id   %lu\n", GetCurrentProcessId());

        char working[MAX_PATH] = {};
        GetCurrentDirectoryA(MAX_PATH, working);

        std::fprintf(g_logFile, "  working dir  %s\n", working);
        std::fflush(g_logFile);

        // Snapshot the modules now, on a normal execution path. The crash
        // handler resolves addresses against this table instead of calling
        // GetModuleFileName, which would take the loader lock.
        CaptureModules();

        LogLoadedModules();
    }

    void Log(const char* format, ...)
    {
        if (!g_initialized || !g_logFile)
        {
            return;
        }

        EnterCriticalSection(&g_lock);

        char timeText[32];
        CurrentTime(timeText, sizeof(timeText));

        std::fprintf(g_logFile, "[%s] ", timeText);

        va_list arguments;
        va_start(arguments, format);
        std::vfprintf(g_logFile, format, arguments);
        va_end(arguments);

        std::fputc('\n', g_logFile);

        // Flush every line: an unflushed buffer is exactly the information we
        // lose when the process dies, which is the whole point of this file.
        std::fflush(g_logFile);

        LeaveCriticalSection(&g_lock);
    }

    void LogBlock(const char* title, const std::string& text)
    {
        if (!g_initialized || !g_logFile)
        {
            return;
        }

        EnterCriticalSection(&g_lock);

        char timeText[32];
        CurrentTime(timeText, sizeof(timeText));

        std::fprintf(g_logFile, "[%s] ==== %s ====\n", timeText, title ? title : "block");
        std::fwrite(text.data(), 1, text.size(), g_logFile);

        if (!text.empty() && text.back() != '\n')
        {
            std::fputc('\n', g_logFile);
        }

        std::fprintf(g_logFile, "[%s] ==== end ====\n", timeText);
        std::fflush(g_logFile);

        LeaveCriticalSection(&g_lock);
    }

    void NameCurrentThread(const char* role)
    {
        t_role = role;
    }

    void Breadcrumb(const char* stage)
    {
        t_stage = stage;

        // Masked rather than taken modulo twice. The double modulo was there to
        // make a negative index safe once the counter wraps past LONG_MAX, and
        // on a signed value the compiler cannot turn either of them into the
        // single AND that a power-of-two size allows. Unsigned wraps by
        // definition, so the correction is not needed and the AND is exact.
        //
        // This runs a dozen times a frame across two threads, which is where a
        // pair of divisions stops being free.
        static_assert((TRAIL_SIZE & (TRAIL_SIZE - 1)) == 0,
            "TRAIL_SIZE must be a power of two for the mask below");

        const unsigned int index =
            static_cast<unsigned int>(InterlockedIncrement(&g_trailIndex) - 1);

        TrailEntry& entry = g_trail[index & (TRAIL_SIZE - 1)];

        entry.threadId = GetCurrentThreadId();
        entry.stage    = stage;
    }

    void InstallCrashHandler()
    {
        if (g_handler)
        {
            return;
        }

        // Last in the vectored chain, so anything that handles its own
        // exceptions gets first refusal and we only see what nobody wanted.
        g_handler = AddVectoredExceptionHandler(0, OnException);

        Log("crash handler installed, reports go to %s", g_crashPath);
    }

    void Shutdown()
    {
        if (g_handler)
        {
            RemoveVectoredExceptionHandler(g_handler);
            g_handler = nullptr;
        }

        if (g_logFile)
        {
            Log("shutting down");
            std::fclose(g_logFile);
            g_logFile = nullptr;
        }

        if (g_initialized)
        {
            DeleteCriticalSection(&g_lock);
            g_initialized = false;
        }
    }

    bool CheckVisualCppRuntime(char* reasonOut, unsigned int reasonSize)
    {
        if (reasonOut && reasonSize)
        {
            reasonOut[0] = '\0';
        }

        // Loaded by definition: this DLL imports it.
        const HMODULE runtime = GetModuleHandleA("msvcp140.dll");

        if (!runtime)
        {
            return true;
        }

        char path[MAX_PATH] = {};

        if (!GetModuleFileNameA(runtime, path, MAX_PATH))
        {
            return true;
        }

        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeA(path, &handle);

        if (!size)
        {
            return true;
        }

        // 4 KB covers a version resource comfortably; anything larger is not
        // one we can read anyway.
        char block[4096];

        if (size > sizeof(block) || !GetFileVersionInfoA(path, 0, size, block))
        {
            return true;
        }

        VS_FIXEDFILEINFO* info = nullptr;
        UINT length = 0;

        if (!VerQueryValueA(block, "\\", reinterpret_cast<LPVOID*>(&info), &length) || !info)
        {
            return true;
        }

        const unsigned int major = HIWORD(info->dwFileVersionMS);
        const unsigned int minor = LOWORD(info->dwFileVersionMS);

        Log("Visual C++ runtime %u.%u.%u.%u at %s",
            major, minor, HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS), path);

        // MSVC 14.40 made std::mutex's constructor constexpr. Code built with
        // 14.40 or later reads a null pointer inside std::mutex::lock when it
        // runs against an older msvcp140, which surfaces as an access violation
        // deep in DirectXTK and looks nothing like what it is.
        if (major > 14 || (major == 14 && minor >= 40))
        {
            return true;
        }

        std::snprintf(g_runtimeWarning, sizeof(g_runtimeWarning),
            "msvcp140.dll is %u.%u.%u.%u - too old. Install the Microsoft Visual C++ "
            "2015-2022 Redistributable (x86); the game is 32-bit, so the x64 one does "
            "not count.",
            major, minor, HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));

        if (reasonOut && reasonSize)
        {
            std::snprintf(reasonOut, reasonSize, "%s", g_runtimeWarning);
        }

        Log("WARNING: %s", g_runtimeWarning);

        return false;
    }

    const char* GetRuntimeWarning()
    {
        return g_runtimeWarning;
    }

    Stage::Stage(const char* stage) : m_previous(t_stage)
    {
        Breadcrumb(stage);
    }

    Stage::~Stage()
    {
        t_stage = m_previous;
    }
}
