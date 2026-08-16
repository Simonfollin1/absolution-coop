#pragma once

#include <cstdint>
#include <string>

// What is left of this after the shared GameOffsets took over.
//
// It used to carry its own Steam and GOG offset tables and its own readers for
// the loading flag, the chapter and the Terminus elevator. All of that is in
// common/Memory/GameOffsets, done properly: the pointer walks are guarded, the
// results are cached rather than re-probed eight times a frame, and the tables
// are the ones every other mod in this set is already using.
//
// What is genuinely local is here: the module's own extents, which the vtable
// patch checks against, and a fingerprint two players can compare so a
// mismatched pair of game builds is reported at the handshake rather than
// discovered by their positions not lining up.
namespace Coop::Game
{
    class BuildInfo
    {
    public:
        static const BuildInfo& Get();

        uintptr_t   ModuleBase() const { return m_moduleBase; }
        uint32_t    SizeOfImage() const { return m_sizeOfImage; }
        bool        OffsetsUsable() const;
        std::string Describe() const;

        // Differs between game builds, which is all it has to do.
        uint32_t Fingerprint() const { return m_sizeOfImage; }

        // True when the address is inside the game's image.
        bool Contains(const void* address) const;

    private:
        BuildInfo();

        uintptr_t m_moduleBase  = 0;
        uint32_t  m_sizeOfImage = 0;
    };
}
