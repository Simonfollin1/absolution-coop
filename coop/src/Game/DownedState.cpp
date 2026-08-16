#include <Windows.h>

#include <algorithm>
#include <format>

#include <Glacier/Player/ZHitman5.h>
#include <Global.h>

#include "Game/DownedState.h"
#include "Game/BuildInfo.h"

namespace Coop::Game
{
    namespace
    {
        // Where YouGotHit sits in IBaseCharacter's vtable.
        //
        // Derived, not guessed. IComponentInterface declares five virtuals -
        // the destructor, GetVariantRef, AddRef, Release, QueryInterface - and
        // IBaseCharacter's own destructor overrides slot 0 rather than adding
        // one. YouGotHit is therefore the first entry it contributes.
        //
        // The SDK's headers are reconstructions, so a virtual the header omits
        // would shift this. That is why ValidateAndPatch checks what it finds
        // before writing anything, and why nothing is patched when the check
        // fails.
        constexpr size_t kYouGotHitSlot = 5;

        // Offsets into SHitInfo, from the 2012 development build's headers:
        //
        //   ZEntityRef        m_HitEntity          +0x00  (4)
        //   ZPhysicsObjectRef m_pHitBody           +0x04  (8)
        //   unsigned int      m_nHitBoneIndex      +0x0C  (4)
        //   IProjectile*      m_pProjectile        +0x10  (4)
        //   float4            m_vHitPos            +0x20  (16, aligned)
        //
        // Only m_vHitPos is read, and only to place a marker. The damage
        // figure lives behind GetBaseDamage(), which is a call we have no
        // address for, so magnitude is not read from here at all - see
        // DamageFromHit.
        constexpr size_t kHitInfoHitPositionOffset = 0x20;

        bool PointerIsExecutable(const void* pointer)
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

            if (info.State != MEM_COMMIT)
            {
                return false;
            }

            constexpr DWORD kExecutable = PAGE_EXECUTE
                                        | PAGE_EXECUTE_READ
                                        | PAGE_EXECUTE_READWRITE
                                        | PAGE_EXECUTE_WRITECOPY;

            return (info.Protect & kExecutable) != 0;
        }

        // The replacement vtable entry. __fastcall with a dummy second
        // argument is how MSVC's __thiscall is spelled when you have to write
        // one by hand on x86: ECX carries this, EDX is unused.
        void __fastcall YouGotHitThunk(void* /*self*/, void* /*edx*/, const void* hitInfo)
        {
            // Deliberately never chains to the original. Not calling it is the
            // whole mechanism: the engine is not told, so the engine cannot
            // start a death sequence, so nobody's world reloads.
            TheDownedState().OnHitIntercepted(hitInfo);
        }
    }

    DownedState& TheDownedState()
    {
        static DownedState instance;

        return instance;
    }

    DownedState::~DownedState()
    {
        Disarm();
    }

    float DownedState::HitPointsFraction() const
    {
        if (m_settings.maxHitPoints <= 0.f)
        {
            return 0.f;
        }

        return std::clamp(m_hitPoints / m_settings.maxHitPoints, 0.f, 1.f);
    }

    void DownedState::ResetForNewSession()
    {
        m_phase        = DownedPhase::Alive;
        m_hitPoints    = m_settings.maxHitPoints;
        m_sinceLastHit = 0.f;
        m_hitsTaken    = 0;
        m_timesDowned  = 0;
    }

    bool DownedState::Arm(ZHitman5* hitman)
    {
        if (!m_settings.enabled)
        {
            Disarm();
            return false;
        }

        if (!hitman)
        {
            // The player entity is gone, which happens on every scene change.
            // The vtable we patched belongs to an object that no longer
            // exists, so let go of it rather than restoring through a dangling
            // pointer later.
            m_armed         = false;
            m_patchedVtable = nullptr;
            m_originalEntry = nullptr;
            m_armedFor      = nullptr;

            return false;
        }

        if (m_armed && m_armedFor == hitman)
        {
            return true;
        }

        // A different player instance means a new scene. Nothing to restore -
        // the old object is gone - so start clean.
        m_armed         = false;
        m_patchedVtable = nullptr;
        m_originalEntry = nullptr;

        // The IBaseCharacter sub-object has its own vtable, and it is that one
        // YouGotHit lives in. Casting through the class hierarchy is what
        // finds it; the address of the ZHitman5 itself points at a different
        // vtable entirely.
        auto* baseCharacter = static_cast<IBaseCharacter*>(static_cast<ZHM5BaseCharacter*>(hitman));

        if (!baseCharacter)
        {
            m_diagnostic = "player does not expose IBaseCharacter";
            return false;
        }

        void** vtable = *reinterpret_cast<void***>(baseCharacter);

        if (!ValidateAndPatch(vtable, kYouGotHitSlot))
        {
            return false;
        }

        m_armedFor  = hitman;
        m_hitPoints = m_settings.maxHitPoints;
        m_phase     = DownedPhase::Alive;

        ApplyEngineGodMode(m_settings.alsoUseEngineGodMode);

        return true;
    }

    bool DownedState::ValidateAndPatch(void** vtable, size_t slot)
    {
        if (!vtable)
        {
            m_diagnostic = "player vtable pointer was null";
            return false;
        }

        const BuildInfo& build = BuildInfo::Get();

        // The vtable itself has to live inside the game's image. A pointer
        // that does not is a sign the cast above landed somewhere it should
        // not have, and writing through it would corrupt whatever is there.
        if (build.ModuleBase() != 0 && build.SizeOfImage() != 0)
        {
            const auto address = reinterpret_cast<uintptr_t>(vtable);

            if (address < build.ModuleBase() ||
                address >= build.ModuleBase() + build.SizeOfImage())
            {
                m_diagnostic = "player vtable is outside HMA.exe - not patching";
                return false;
            }
        }

        // Every entry up to and including the one being replaced must look
        // like code. If the reconstructed class layout is wrong, the odds of
        // six consecutive plausible function pointers are slim, and being
        // wrong here means replacing something that is not YouGotHit.
        for (size_t i = 0; i <= slot; ++i)
        {
            if (!PointerIsExecutable(vtable[i]))
            {
                m_diagnostic = std::format(
                    "vtable entry {} is not executable - class layout does not match, "
                    "damage interception stays off", i);

                return false;
            }
        }

        DWORD previousProtection = 0;

        if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &previousProtection))
        {
            m_diagnostic = "could not make the vtable writable";
            return false;
        }

        m_originalEntry = vtable[slot];
        vtable[slot]    = reinterpret_cast<void*>(&YouGotHitThunk);

        DWORD restored = 0;
        VirtualProtect(&vtable[slot], sizeof(void*), previousProtection, &restored);

        m_patchedVtable = vtable;
        m_patchedSlot   = slot;
        m_armed         = true;
        m_diagnostic    = "damage interception armed";

        return true;
    }

    void DownedState::Disarm()
    {
        if (m_armed && m_patchedVtable && m_originalEntry)
        {
            DWORD previousProtection = 0;

            if (VirtualProtect(&m_patchedVtable[m_patchedSlot], sizeof(void*),
                               PAGE_READWRITE, &previousProtection))
            {
                m_patchedVtable[m_patchedSlot] = m_originalEntry;

                DWORD restored = 0;
                VirtualProtect(&m_patchedVtable[m_patchedSlot], sizeof(void*),
                               previousProtection, &restored);
            }
        }

        if (m_armed)
        {
            ApplyEngineGodMode(false);
        }

        m_armed         = false;
        m_patchedVtable = nullptr;
        m_originalEntry = nullptr;
        m_armedFor      = nullptr;
    }

    void DownedState::ApplyEngineGodMode(bool enable)
    {
        if (!m_settings.alsoUseEngineGodMode)
        {
            return;
        }

        const BuildInfo& build = BuildInfo::Get();

        // Steam only, and even there the SDK's own two mods disagree about
        // which address this is - Player says 0xD4F5E0, Actors says 0xD4D91C.
        // Neither has been confirmed here, so this is a backstop for damage
        // paths that never reach YouGotHit, not the mechanism. When the build
        // is not Steam it simply does not run, and the vtable hook carries the
        // feature on its own.
        if (build.Build() != GameBuild::Steam)
        {
            return;
        }

        constexpr uintptr_t kGodModeOffset = 0xD4F5E0;

        if (kGodModeOffset >= build.SizeOfImage())
        {
            return;
        }

        auto* flag = reinterpret_cast<int*>(build.ModuleBase() + kGodModeOffset);

        __try
        {
            *flag = enable ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // An address that is not writable is an address that is not this.
        }
    }

    bool DownedState::DamageFromHit(const void* hitInfo, float& outDamage) const
    {
        // SHitInfo carries its damage behind GetBaseDamage() and
        // GetExplosionDamage(), which read a projectile's own configuration
        // and an explosion sub-struct. Both are calls, neither has a retail
        // address, and the arithmetic behind them is not something to
        // reimplement from a header.
        //
        // So magnitude is not recovered. Every hit costs the same, which is
        // wrong in detail and right in shape: a firefight still kills you,
        // and it does so in a predictable number of hits that the player can
        // learn. Reporting a confident number we cannot compute would be worse.
        (void)hitInfo;

        outDamage = m_settings.fallbackDamagePerHit;

        return false;
    }

    void DownedState::OnHitIntercepted(const void* hitInfo)
    {
        ++m_hitsTaken;
        m_sinceLastHit = 0.f;

        if (m_phase != DownedPhase::Alive)
        {
            // Already down. Further hits land on someone the engine cannot
            // see anyway; ignoring them keeps the counter honest.
            return;
        }

        float damage = m_settings.fallbackDamagePerHit;
        DamageFromHit(hitInfo, damage);

        m_hitPoints -= damage;

        if (m_hitPoints <= 0.f)
        {
            m_hitPoints = 0.f;
            GoDown();
        }
    }

    void DownedState::GoDown()
    {
        if (m_phase == DownedPhase::Downed)
        {
            return;
        }

        m_phase = DownedPhase::Downed;
        ++m_timesDowned;
    }

    void DownedState::Update(float deltaSeconds, bool engineReportsDead)
    {
        if (!m_settings.enabled)
        {
            return;
        }

        // The second signal. Falls, drowning and scripted kills very likely
        // never reach YouGotHit, so if the engine says dead in spite of
        // everything above, that is a death and it is treated as one.
        if (engineReportsDead && m_phase == DownedPhase::Alive)
        {
            m_hitPoints = 0.f;
            GoDown();
        }

        if (m_phase != DownedPhase::Alive)
        {
            return;
        }

        m_sinceLastHit += deltaSeconds;

        if (m_sinceLastHit >= m_settings.regenDelaySeconds && m_hitPoints < m_settings.maxHitPoints)
        {
            m_hitPoints = std::min(m_settings.maxHitPoints,
                                   m_hitPoints + m_settings.regenPerSecond * deltaSeconds);
        }
    }

    void DownedState::ReviveOnSceneChange()
    {
        if (m_phase == DownedPhase::Alive)
        {
            return;
        }

        m_phase        = DownedPhase::Recovering;
        m_hitPoints    = m_settings.maxHitPoints;
        m_sinceLastHit = 0.f;
    }

    void DownedState::ReviveOnCheckpoint()
    {
        ReviveOnSceneChange();
    }
}
