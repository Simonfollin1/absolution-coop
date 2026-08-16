#pragma once

#include <cstdint>
#include <string>

// Which shipped build of the game this process is, and the handful of state
// flags that only exist as raw memory.
//
// Everything here is offset-dependent and therefore build-dependent. When the
// build is not recognised, every accessor reports "unknown" rather than
// reading an address that means something else - a co-op session that says it
// does not know what chapter you are on is annoying, one that reads a random
// pointer is a crash.
namespace Coop::Game
{
    enum class GameBuild : uint8_t
    {
        Unknown = 0,
        Steam   = 1,
        GOG     = 2,
    };

    class BuildInfo
    {
    public:
        // Resolved once, on first use.
        static const BuildInfo& Get();

        GameBuild   Build() const { return m_build; }
        uintptr_t   ModuleBase() const { return m_moduleBase; }
        uint32_t    SizeOfImage() const { return m_sizeOfImage; }
        bool        OffsetsUsable() const { return m_build != GameBuild::Unknown; }
        std::string Describe() const;

        // A cheap value that differs between game builds, so two players on
        // mismatched executables find out at the handshake instead of by
        // wondering why their positions do not line up.
        uint32_t Fingerprint() const { return m_sizeOfImage; }

        // ---- Engine state that is only reachable by offset ----------------
        //
        // Chapter and section identify "where in the campaign" a player is.
        // 0xFF means the value could not be read.

        bool    IsLoading() const;
        bool    IsInMenu() const;
        uint8_t CurrentLevel() const;
        uint8_t CurrentSection() const;

    private:
        BuildInfo();

        template <typename T>
        bool Read(uintptr_t offset, T& out) const;

        struct OffsetTable
        {
            uintptr_t isLoadingScreen;
            uintptr_t isInMenu;
            uintptr_t currentLevel;
            uintptr_t currentSection;
            uintptr_t elevatorPointer;  // Terminus lift, a second loading state
        };

        GameBuild   m_build       = GameBuild::Unknown;
        uintptr_t   m_moduleBase  = 0;
        uint32_t    m_sizeOfImage = 0;
        OffsetTable m_offsets{};
    };
}
