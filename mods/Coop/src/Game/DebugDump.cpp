#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Player/ZHitman5.h>
#include <Glacier/Actor/ZActor.h>
#include <Glacier/Actor/ZActorManager.h>
#include <Glacier/CheckPoint/ZCheckPointManager.h>
#include <Glacier/CheckPoint/ZCheckPointManagerEntity.h>
#include <Global.h>

#include "Game/DebugDump.h"
#include "Game/BuildInfo.h"
#include "Game/ConfigVars.h"
#include "Game/DownedState.h"
#include "Game/HitInspector.h"
#include "Game/WorldProbe.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    namespace
    {
        // Everything below reads memory the mod does not own, so every read is
        // guarded. POD only in these, because __try cannot share a function
        // with anything that needs unwinding.
        bool ReadGuarded(const void* source, void* destination, size_t count)
        {
            __try
            {
                memcpy(destination, source, count);

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool CopyStringGuarded(const char* source, char* destination, size_t capacity)
        {
            __try
            {
                size_t i = 0;

                for (; i + 1 < capacity && source[i] != '\0'; ++i)
                {
                    destination[i] = source[i];
                }

                destination[i] = '\0';

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destination[0] = '\0';

                return false;
            }
        }

        bool PageIs(const void* pointer, DWORD mask)
        {
            if (!pointer)
            {
                return false;
            }

            MEMORY_BASIC_INFORMATION info{};

            if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info))
            {
                return false;
            }

            return info.State == MEM_COMMIT && (info.Protect & mask) != 0;
        }

        bool IsCode(const void* pointer)
        {
            constexpr DWORD kExecutable = PAGE_EXECUTE
                                        | PAGE_EXECUTE_READ
                                        | PAGE_EXECUTE_READWRITE
                                        | PAGE_EXECUTE_WRITECOPY;

            return PageIs(pointer, kExecutable);
        }

        bool IsReadable(const void* pointer)
        {
            constexpr DWORD kReadable = PAGE_READONLY
                                      | PAGE_READWRITE
                                      | PAGE_WRITECOPY
                                      | PAGE_EXECUTE_READ
                                      | PAGE_EXECUTE_READWRITE
                                      | PAGE_EXECUTE_WRITECOPY;

            return PageIs(pointer, kReadable);
        }

        uintptr_t ToRva(uintptr_t address)
        {
            const uintptr_t base = GameOffsets::GetModuleBase();

            return (base && address > base) ? address - base : address;
        }

        // MSVC's complete object locator, 32-bit layout. Only the type
        // descriptor pointer is wanted, and only its name.
        std::string NameFromVtable(void** vtable)
        {
            if (!vtable || !IsReadable(vtable - 1))
            {
                return {};
            }

            void* locator = nullptr;

            if (!ReadGuarded(vtable - 1, &locator, sizeof(locator)) || !IsReadable(locator))
            {
                return {};
            }

            // signature, offset, cdOffset, pTypeDescriptor - so the descriptor
            // is the fourth dword.
            void* typeDescriptor = nullptr;

            if (!ReadGuarded(static_cast<uint8_t*>(locator) + 12,
                             &typeDescriptor, sizeof(typeDescriptor)))
            {
                return {};
            }

            if (!IsReadable(typeDescriptor))
            {
                return {};
            }

            // vftable, spare, then the decorated name as a plain char array.
            const char* name = reinterpret_cast<const char*>(
                static_cast<uint8_t*>(typeDescriptor) + 8);

            char buffer[256] = {};

            if (!CopyStringGuarded(name, buffer, sizeof(buffer)))
            {
                return {};
            }

            return buffer;
        }
    }

    std::vector<VtableReport> ScanVtables(const void* object, size_t searchBytes)
    {
        std::vector<VtableReport> found;

        if (!object || !IsReadable(object))
        {
            return found;
        }

        const BuildInfo& build = BuildInfo::Get();

        for (size_t offset = 0; offset + sizeof(void*) <= searchBytes; offset += sizeof(void*))
        {
            void* candidate = nullptr;

            if (!ReadGuarded(static_cast<const uint8_t*>(object) + offset,
                             &candidate, sizeof(candidate)))
            {
                continue;
            }

            // A vtable lives in the image's read-only data and its first entry
            // is code. Both together are a strong enough filter that no scan of
            // a real object has produced a false positive.
            if (!candidate || !build.Contains(candidate) || !IsReadable(candidate))
            {
                continue;
            }

            void** vtable = static_cast<void**>(candidate);

            void* firstEntry = nullptr;

            if (!ReadGuarded(vtable, &firstEntry, sizeof(firstEntry)) || !IsCode(firstEntry))
            {
                continue;
            }

            VtableReport report;

            report.objectOffset  = offset;
            report.vtableRva     = ToRva(reinterpret_cast<uintptr_t>(vtable));
            report.decoratedName = NameFromVtable(vtable);

            for (size_t i = 0; i < 64; ++i)
            {
                void* entry = nullptr;

                if (!ReadGuarded(vtable + i, &entry, sizeof(entry)) || !IsCode(entry))
                {
                    break;
                }

                report.entryRvas.push_back(ToRva(reinterpret_cast<uintptr_t>(entry)));
            }

            found.push_back(std::move(report));
        }

        return found;
    }

    std::string WriteDump(const ConfigVars& configVars,
                          const WorldProbe& probe,
                          const std::string& bindingExpression,
                          const std::vector<std::string>& log,
                          std::string& error)
    {
        error.clear();

        std::error_code code;
        std::filesystem::path directory = std::filesystem::current_path(code);

        if (code)
        {
            error = "could not work out the game folder";
            return {};
        }

        // Numbered rather than timestamped, so a sequence of them sorts in the
        // order they were taken and nobody has to read a clock.
        std::filesystem::path path;

        for (int i = 1; i < 1000; ++i)
        {
            path = directory / std::format("coop-dump-{:03}.txt", i);

            if (!std::filesystem::exists(path))
            {
                break;
            }
        }

        std::ofstream file(path, std::ios::out | std::ios::trunc);

        if (!file)
        {
            error = std::format("could not write {}", path.string());
            return {};
        }

        const BuildInfo& build = BuildInfo::Get();
        DownedState&     downed = TheDownedState();

        file << "Absolution Co-op dump\n";
        file << "=====================\n\n";

        file << "Build: " << build.Describe() << "\n";
        file << std::format("Detected as: {}\n", GameOffsets::GetBuildName());
        file << std::format("Module base: {:08X}, size {} ({:X})\n",
                            build.ModuleBase(), build.SizeOfImage(), build.SizeOfImage());
        file << std::format("Offsets usable: {}\n\n", build.OffsetsUsable() ? "yes" : "no");

        file << "-- Keys ------------------------------------------------------\n";
        file << (bindingExpression.empty() ? "(nothing was registered)" : bindingExpression);
        file << "\n\n";

        file << "-- State -----------------------------------------------------\n";
        file << std::format("Chapter {}, section {}, checkpoint (jump point) {}\n",
                            probe.LocalState().level, probe.LocalState().section,
                            probe.CurrentJumpPoint());
        file << std::format("Loading: {}, in menu: {}\n",
                            GameOffsets::IsLoading() ? "yes" : "no",
                            GameOffsets::IsInMenu() ? "yes" : "no");
        file << std::format("Damage interception: {}\n", downed.IsArmed() ? "armed" : "not armed");
        file << "  " << downed.Diagnostic() << "\n";
        file << "  " << downed.ImmunityNote() << "\n";
        file << std::format("Health {:.0f}/{:.0f}, {} hits taken, downed {} times\n\n",
                            downed.HitPoints(), downed.Settings().maxHitPoints,
                            downed.HitsTaken(), downed.TimesDowned());

        // -- Hits ---------------------------------------------------------
        //
        // The reason this file exists. Every intercepted hit, as raw bytes,
        // plus who called - which is the damage code.
        const HitInspector&           hits     = TheHitInspector();
        const std::vector<HitCapture> captures = hits.Snapshot();

        file << "-- Hits ------------------------------------------------------\n";
        file << std::format("{} intercepted, {} kept\n\n", hits.Total(), captures.size());

        file << "Callers (open these in a disassembler at base + rva):\n";

        for (const auto& [rva, count] : hits.Callers())
        {
            file << std::format("  {:08X}  {} hits\n", rva, count);
        }

        file << "\nSHitInfo, by dword offset. 'varies' marks the slots that were\n";
        file << "not the same in every hit - the damage figure is one of those.\n\n";

        constexpr size_t kDwords = kHitCaptureBytes / sizeof(uint32_t);

        for (size_t i = 0; i < kDwords; ++i)
        {
            const std::vector<uint32_t> values = hits.ValuesAt(i);

            if (values.empty())
            {
                continue;
            }

            file << std::format("  +0x{:02X}  {:<8}", i * sizeof(uint32_t),
                                values.size() > 1 ? "varies" : "same");

            for (size_t v = 0; v < values.size() && v < 6; ++v)
            {
                file << "  " << DescribeDword(values[v]);
            }

            file << "\n";
        }

        file << "\nRaw, newest first:\n";

        for (const HitCapture& capture : captures)
        {
            file << std::format("\n  hit {} at {:08X}, called from +{:08X}, {} bytes read{}\n",
                                capture.ordinal, capture.address, capture.callerRva,
                                capture.byteCount,
                                capture.readable ? "" : "  (UNREADABLE)");

            if (!capture.readable)
            {
                continue;
            }

            for (size_t i = 0; i + 16 <= capture.byteCount; i += 16)
            {
                file << std::format("    +{:02X} ", i);

                for (size_t b = 0; b < 16; ++b)
                {
                    file << std::format("{:02X} ", capture.bytes[i + b]);
                }

                file << "\n";
            }
        }

        file << "\n";

        // -- Vtables ------------------------------------------------------
        file << "-- Player vtables --------------------------------------------\n";
        file << "Every vtable pointer in the first 0x40 bytes of ZHitman5, named\n";
        file << "by its own RTTI, with entries as RVAs.\n\n";

        ZHitman5* hitman = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr;

        if (!hitman)
        {
            file << "  (no player right now - take this dump in a level)\n";
        }

        for (const VtableReport& report : ScanVtables(hitman))
        {
            file << std::format("  +0x{:02X}  vtable {:08X}  {}  ({} entries)\n",
                                report.objectOffset, report.vtableRva,
                                report.decoratedName.empty() ? "(no RTTI)" : report.decoratedName,
                                report.entryRvas.size());

            for (size_t i = 0; i < report.entryRvas.size(); ++i)
            {
                file << std::format("      [{:2}] {:08X}\n", i, report.entryRvas[i]);
            }

            file << "\n";
        }

        // -- Actors -------------------------------------------------------
        file << "-- Actors ----------------------------------------------------\n";
        file << std::format("{} listed as alive, {} of them flagged dead\n",
                            probe.AliveActorCount(), probe.DeadFlaggedCount());
        file << std::format("Deaths seen: {} by flag, {} by leaving the list\n\n",
                            probe.DeathsByFlag(), probe.DeathsByVanish());

        if (ActorManager)
        {
            TArrayRef<TEntityRef<ZActor>> actors = ActorManager->GetAliveActors();

            for (size_t i = 0; i < actors.Size(); ++i)
            {
                ZActor* actor = actors[i].GetRawPointer();

                if (!actor)
                {
                    continue;
                }

                const char* name = actor->GetActorName().ToCString();
                const float4 position = actor->GetWorldPosition();

                file << std::format("  {:<40} {:<6} {:8.1f} {:8.1f} {:8.1f}\n",
                                    name ? name : "(unnamed)",
                                    actor->IsDead() ? "dead" : "alive",
                                    position.x, position.y, position.z);
            }
        }

        file << "\n";

        // -- Config -------------------------------------------------------
        file << "-- Engine configuration --------------------------------------\n";

        if (!configVars.Walked())
        {
            file << "  (not read yet - press Read them on the Engine tab first)\n";
        }
        else
        {
            file << std::format("{} variables\n\n", configVars.All().size());

            for (const ConfigVar& entry : configVars.All())
            {
                file << std::format("  {:<52} {:<7} {}\n", entry.name,
                                    entry.type == ConfigVarType::Float  ? "float"
                                  : entry.type == ConfigVarType::Int    ? "int"
                                  : entry.type == ConfigVarType::String ? "string"
                                                                        : "?",
                                    entry.ValueText());
            }
        }

        file << "\n-- Log -------------------------------------------------------\n";

        for (const std::string& line : log)
        {
            file << "  " << line << "\n";
        }

        file << "\n";
        file.close();

        return path.string();
    }
}
