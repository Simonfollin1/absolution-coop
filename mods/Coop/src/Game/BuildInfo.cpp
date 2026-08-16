#include <Windows.h>

#include <format>

#include "Game/BuildInfo.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    BuildInfo::BuildInfo()
    {
        // Never by name. The executable is HMA.exe, so asking for anything
        // else returns null - the same trap GameOffsets documents.
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
    }

    const BuildInfo& BuildInfo::Get()
    {
        static const BuildInfo instance;

        return instance;
    }

    bool BuildInfo::OffsetsUsable() const
    {
        return GameOffsets::IsSupported();
    }

    std::string BuildInfo::Describe() const
    {
        if (!GameOffsets::IsSupported())
        {
            return std::format("unrecognised build (image size {} bytes) - "
                               "chapter and loading detection are off",
                               m_sizeOfImage);
        }

        return std::format("Hitman: Absolution 1.0.447.0 ({})", GameOffsets::GetBuildName());
    }

    bool BuildInfo::Contains(const void* address) const
    {
        if (m_moduleBase == 0 || m_sizeOfImage == 0)
        {
            return false;
        }

        const auto value = reinterpret_cast<uintptr_t>(address);

        return value >= m_moduleBase && value < m_moduleBase + m_sizeOfImage;
    }
}
