#include <Windows.h>

#include <format>

#include "Game/BuildInfo.h"

namespace Coop::Game
{
    namespace
    {
        // Module sizes are the discriminator LiveSplit's Absolution component
        // uses, and the only one available before any offset has been trusted.
        constexpr uint32_t kSteamSizeOfImage = 36777984u;
        constexpr uint32_t kGogSizeOfImage   = 35799040u;

        // Source for both offset tables below: SuiMachine/LiveSplit.HitmanAbsolution.
        // Cross-checked against mattiejastje/HitmanTracker, which keeps its own
        // separate GOG table - the two stores do not share a layout, whatever
        // SDK issue #7 suggests.
    }

    template <typename T>
    bool BuildInfo::Read(uintptr_t offset, T& out) const
    {
        if (m_build == GameBuild::Unknown || m_moduleBase == 0 || offset == 0)
        {
            return false;
        }

        const uintptr_t address = m_moduleBase + offset;

        // The offset must land inside the module. A table entry that is wrong
        // for this build would otherwise be a read into whatever the allocator
        // last put there.
        if (offset >= m_sizeOfImage)
        {
            return false;
        }

        __try
        {
            out = *reinterpret_cast<const T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return true;
    }

    BuildInfo::BuildInfo()
    {
        // GetModuleHandleA(nullptr) - never by name. The executable is
        // HMA.exe, not HitmanAbsolution.exe, and asking for the wrong name
        // returns null, which turns every subsequent read into a dereference
        // of a near-null address.
        const HMODULE module = GetModuleHandleA(nullptr);

        if (!module)
        {
            return;
        }

        m_moduleBase = reinterpret_cast<uintptr_t>(module);

        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);

        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }

        const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
            m_moduleBase + dosHeader->e_lfanew);

        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }

        m_sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;

        if (m_sizeOfImage == kSteamSizeOfImage)
        {
            m_build   = GameBuild::Steam;
            m_offsets = OffsetTable{ 0xE53E20, 0xD61C7B, 0xE20F48, 0xD60F94, 0xE39520 };
        }
        else if (m_sizeOfImage == kGogSizeOfImage)
        {
            m_build   = GameBuild::GOG;
            m_offsets = OffsetTable{ 0xC96750, 0xCA983B, 0xD68B08, 0xCA8B74, 0xC939D0 };
        }
    }

    const BuildInfo& BuildInfo::Get()
    {
        static const BuildInfo instance;

        return instance;
    }

    std::string BuildInfo::Describe() const
    {
        switch (m_build)
        {
        case GameBuild::Steam:
            return "Hitman: Absolution 1.0.447.0 (Steam)";
        case GameBuild::GOG:
            return "Hitman: Absolution 1.0.447.0 (GOG)";
        default:
            return std::format("unrecognised build (image size {} bytes) - "
                               "chapter and loading detection are off",
                               m_sizeOfImage);
        }
    }

    bool BuildInfo::IsLoading() const
    {
        bool loading = false;

        if (Read(m_offsets.isLoadingScreen, loading) && loading)
        {
            return true;
        }

        // The Terminus lift is a loading screen that does not set the loading
        // flag. Without this a co-op timer runs through it on one machine and
        // not the other.
        uintptr_t elevator = 0;

        if (Read(m_offsets.elevatorPointer, elevator) && elevator != 0)
        {
            __try
            {
                if (*reinterpret_cast<const bool*>(elevator + 0x38))
                {
                    return true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        return false;
    }

    bool BuildInfo::IsInMenu() const
    {
        bool inMenu = false;

        return Read(m_offsets.isInMenu, inMenu) && inMenu;
    }

    uint8_t BuildInfo::CurrentLevel() const
    {
        int level = 0;

        if (!Read(m_offsets.currentLevel, level) || level < 0 || level > 25)
        {
            return 0xFF;
        }

        return static_cast<uint8_t>(level);
    }

    uint8_t BuildInfo::CurrentSection() const
    {
        int section = 0;

        if (!Read(m_offsets.currentSection, section) || section < 0 || section > 254)
        {
            return 0xFF;
        }

        return static_cast<uint8_t>(section);
    }
}
