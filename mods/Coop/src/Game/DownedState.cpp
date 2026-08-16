#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstring>
#include <format>

#include <Glacier/Player/ZHitman5.h>
#include <Global.h>

#include "Game/DownedState.h"
#include "Game/BuildInfo.h"
#include "Game/HitInspector.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    namespace
    {
        // Where YouGotHit sits in IBaseCharacter's vtable.
        //
        // Confirmed against the shipping binary, not derived and hoped for.
        // Recovering ZHM5BaseCharacter's vtables through RTTI gives five
        // tables whose entry counts match the SDK headers exactly:
        //
        //   0x00  ZEntityImpl                      24 entries
        //   0x08  IHM5BaseCharacter                 5   (adds nothing)
        //   0x0c  IBaseCharacter                   12
        //   0x10  IMorphemeCutSequenceAnimatable   11
        //   0x14  IBoneCollidable                   3
        //
        // Five independent agreements, so the headers are faithful and the
        // base order is the declared one. In the 0x0c table slots 2 and 3 are
        // the same function - AddRef and Release, both trivial - slot 4 is
        // QueryInterface, and slot 5 is the first substantial entry. That is
        // YouGotHit, at 0x0080df50 in ZHM5BaseCharacter's own implementation.
        //
        // ZHitman5 carries no type descriptor of its own, but it does not need
        // one: it derives from ZHM5BaseCharacter, and an inherited vtable keeps
        // its indices. Slot 5 holds for the player too.
        constexpr size_t kYouGotHitSlot = 5;

        // IComponentInterface's AddRef and Release are separate declarations
        // compiling to the same trivial body, so the linker folds them onto one
        // address. Every vtable derived from it in the dump shows slots 2 and 3
        // identical, and the one interface that does not derive from it -
        // IMorphemeCutSequenceAnimatable - does not.
        //
        // That makes it a fingerprint rather than a guess, and a far better
        // check than "these look like code": every entry in a vtable looks like
        // code, so the old check could not have caught being off by one.
        constexpr size_t kAddRefSlot  = 2;
        constexpr size_t kReleaseSlot = 3;

        // IBaseCharacter declares exactly twelve virtuals. Advisory rather than
        // blocking: reading past a table's end can land on the next one in
        // .rdata, which is still a code pointer.
        constexpr size_t kExpectedEntryCount = 12;

        // Nothing here reads SHitInfo's fields. What it contains on the retail
        // build, measured from 256 captured hits rather than taken from the
        // 2012 headers - which are wrong about it - is documented where it is
        // used, in Game/HitInspector.h, and in docs/RE-NOTES.md.

        // The player god-mode flag, as an offset from the game module's base.
        //
        // This is the SDK's own address - Mods/Player writes it behind the "God
        // Mode" checkbox - so it is as confirmed as anything else the SDK does,
        // and it is a shipped feature people use. It is a static int inside
        // HMA.exe rather than a pointer to walk.
        //
        // Not to be confused with 0xD4D91C, which the Actors mod writes: that
        // one is god mode for NPCs, a different flag for a different job.
        constexpr uintptr_t kPlayerGodModeOffset = 0xD4F5E0;

        // POD only and no destructors in scope, because __try cannot coexist
        // with anything that needs unwinding.
        bool CopyPlayerBytes(const void* source, uint8_t* destination, size_t count)
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
        void __fastcall YouGotHitThunk(void* self, void* /*edx*/, const void* hitInfo)
        {
            // Where the call came from. That is the code which built the
            // SHitInfo, so it is where the damage figure was computed - the one
            // thing a disassembler cannot tell us on its own, because YouGotHit
            // is only ever reached through a vtable and has no direct callers.
            //
            // _ReturnAddress is an intrinsic, so this costs nothing.
            TheHitInspector().Capture(hitInfo,
                                      reinterpret_cast<uintptr_t>(_ReturnAddress()));

            // Normally never chains to the original. Not calling it is the
            // whole mechanism: the engine is not told, so the engine cannot
            // start a death sequence, so nobody's world reloads.
            //
            // The exception is the research switch below, which exists to find
            // where the engine keeps the player's real health: let exactly one
            // hit through, and whatever changed in the player object is it.
            TheDownedState().OnHitIntercepted(hitInfo);

            if (TheDownedState().ConsumePassThrough())
            {
                TheDownedState().CallOriginalYouGotHit(self, hitInfo);
            }
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

    float DownedState::SecondsUntilSelfRevive() const
    {
        if (m_phase != DownedPhase::Downed || m_settings.selfReviveSeconds <= 0.f)
        {
            return 0.f;
        }

        return std::max(0.f, m_settings.selfReviveSeconds - m_downedSeconds);
    }

    float DownedState::SecondsOfGraceLeft() const
    {
        if (m_phase != DownedPhase::Recovering)
        {
            return 0.f;
        }

        return std::max(0.f, m_settings.recoveryGraceSeconds - m_recoveringSeconds);
    }

    void DownedState::ResetForNewSession()
    {
        m_phase        = DownedPhase::Alive;
        m_hitPoints    = m_settings.maxHitPoints;
        m_sinceLastHit = 0.f;
        m_hitsTaken     = 0;
        m_timesDowned   = 0;
        m_downedSeconds = 0.f;
        m_recoveringSeconds = 0.f;
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

        m_armedFor          = hitman;
        m_hitPoints         = m_settings.maxHitPoints;
        m_phase             = DownedPhase::Alive;
        m_recoveringSeconds = 0.f;

        // A new scene is a fair place to reconsider a refusal - the flag may
        // have held something unrecognisable at the moment we last looked.
        m_immunityRefused = false;

        // A different character entirely, and one that is not on the floor.
        m_ragdollPending = false;
        m_standUpPending = false;
        m_ragdollActive  = false;

        ApplyEngineImmunity(true);

        return true;
    }

    bool DownedState::ValidateAndPatch(void** vtable, size_t slot)
    {
        if (!vtable)
        {
            m_diagnostic = "player vtable pointer was null";
            return false;
        }

        // The vtable itself has to live inside the game's image. A pointer
        // that does not is a sign the cast above landed somewhere it should
        // not have, and writing through it would corrupt whatever is there.
        const BuildInfo& build = BuildInfo::Get();

        if (build.SizeOfImage() != 0 && !build.Contains(vtable))
        {
            m_diagnostic = "player vtable is outside HMA.exe - not patching";
            return false;
        }

        // Every entry up to and including the one being replaced must be code.
        // This catches a pointer that landed somewhere it should not have; it
        // does not catch being off by one, because every entry in a vtable is
        // code. The fingerprint below is what catches that.
        for (size_t i = 0; i <= slot; ++i)
        {
            if (!PointerIsExecutable(vtable[i]))
            {
                m_diagnostic = std::format(
                    "vtable entry {} is not executable - this is not the table "
                    "we think it is, damage interception stays off", i);

                return false;
            }
        }

        // The IComponentInterface fingerprint: AddRef and Release fold onto one
        // address. If slots 2 and 3 differ, either this is not an
        // IComponentInterface-derived vtable or the layout has shifted - and in
        // both cases slot 5 is not YouGotHit.
        if (vtable[kAddRefSlot] != vtable[kReleaseSlot])
        {
            m_diagnostic =
                "vtable slots 2 and 3 differ, so this is not the IBaseCharacter "
                "table - damage interception stays off";

            return false;
        }

        // How long the table actually is. Twelve is what IBaseCharacter should
        // have; anything else is worth saying out loud without refusing to
        // arm, because a table butting up against the next one in .rdata reads
        // as longer than it is.
        size_t entryCount = 0;

        while (entryCount < 64 && PointerIsExecutable(vtable[entryCount]))
        {
            ++entryCount;
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

        m_diagnostic = entryCount == kExpectedEntryCount
            ? std::format("damage interception armed, slot {} of a {}-entry "
                          "IBaseCharacter table", slot, entryCount)
            : std::format("damage interception armed, slot {} - but the table "
                          "reads as {} entries rather than {}, so check this "
                          "before trusting it", slot, entryCount, kExpectedEntryCount);

        return true;
    }

    void DownedState::Disarm()
    {
        // Stand the player up before letting go of them. Unloading the mod
        // while 47 is face down on the floor would leave the game with a
        // problem it has no way to explain.
        if (m_ragdollActive)
        {
            m_ragdollPending = false;
            m_standUpPending = true;

            ServiceRagdoll();
        }

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

        // Unconditional: it restores only a write it made, and leaving god mode
        // on after the mod has gone would be somebody else's mystery.
        ApplyEngineImmunity(false);

        m_armed           = false;
        m_immunityRefused = false;
        m_patchedVtable   = nullptr;
        m_originalEntry   = nullptr;
        m_armedFor        = nullptr;
    }

    void DownedState::ApplyEngineImmunity(bool enable)
    {
        // The backstop, not the mechanism. The vtable patch catches ordinary
        // damage; this covers the paths that never reach YouGotHit at all -
        // falls, drowning, scripted kills - where the engine would otherwise
        // run its death sequence and reload a checkpoint, which in a session
        // resets one player's whole world while the others carry on.
        if (!enable)
        {
            // Only undo a write we actually made, and put back what was there
            // rather than assuming it was off. Somebody may have had the SDK's
            // own God Mode checkbox ticked before we arrived.
            if (m_immunityApplied)
            {
                *reinterpret_cast<int*>(GameOffsets::GetModuleBase() + kPlayerGodModeOffset) =
                    m_immunityPrevious;

                m_immunityApplied = false;
                m_immunityNote    = "god-mode flag restored";
            }

            return;
        }

        if (!m_settings.alsoUseEngineImmunity)
        {
            m_immunityNote = "engine backstop off - only intercepted damage is caught";
            return;
        }

        if (m_immunityApplied)
        {
            return;
        }

        // Every path below this either writes or refuses, and a refusal holds
        // until something changes - Arm is what clears it.
        m_immunityRefused = true;

        // Steam only. The address is the SDK's, and every address the SDK has
        // is for the build it was written against; the GOG executable has a
        // different layout, so the same offset there is a write into whatever
        // else happens to live at it.
        if (GameOffsets::GetBuild() != GameOffsets::Build::Steam)
        {
            m_immunityNote = std::format(
                "engine backstop off on the {} build - the god-mode address is "
                "only known for Steam. Falls and drowning still kill you.",
                GameOffsets::GetBuildName());

            return;
        }

        int* flag = reinterpret_cast<int*>(GameOffsets::GetModuleBase() + kPlayerGodModeOffset);

        if (!BuildInfo::Get().Contains(flag))
        {
            m_immunityNote = "god-mode flag is outside HMA.exe - not writing";
            return;
        }

        // It is a bool the engine stores in an int, so anything else means we
        // are looking at the wrong thing and should keep our hands off it.
        const int previous = *flag;

        if (previous != 0 && previous != 1)
        {
            m_immunityNote = std::format(
                "god-mode flag reads {} rather than 0 or 1 - not writing to it",
                previous);

            return;
        }

        m_immunityPrevious = previous;
        *flag              = 1;
        m_immunityApplied  = true;
        m_immunityRefused  = false;

        m_immunityNote = previous == 1
            ? "god-mode flag was already set - left on"
            : "god-mode flag set, so falls and scripted kills cannot end the run";
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
            // Down, or just up and still inside the grace window. Either way
            // the hit is counted and costs nothing - being dropped again in
            // the second after standing up is a loop, not a fight.
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

    void DownedState::ArmPassThrough(uint32_t hits)
    {
        m_passThroughRemaining = hits;
    }

    bool DownedState::ConsumePassThrough()
    {
        if (m_passThroughRemaining == 0)
        {
            return false;
        }

        --m_passThroughRemaining;

        return true;
    }

    void DownedState::CallOriginalYouGotHit(void* self, const void* hitInfo) const
    {
        if (!m_armed || !m_originalEntry || !self)
        {
            return;
        }

        // __thiscall by hand: ECX holds this, EDX is dead, the argument is on
        // the stack. Same convention the thunk itself is written in.
        using Original = void(__fastcall*)(void*, void*, const void*);

        reinterpret_cast<Original>(m_originalEntry)(self, nullptr, hitInfo);
    }

    size_t DownedState::SnapshotPlayer(uint8_t* destination, size_t count) const
    {
        if (!m_armedFor || !destination || count == 0)
        {
            return 0;
        }

        return CopyPlayerBytes(m_armedFor, destination, count) ? count : 0;
    }

    void DownedState::GoDown()
    {
        if (m_phase == DownedPhase::Downed)
        {
            return;
        }

        m_phase         = DownedPhase::Downed;
        m_downedSeconds = 0.f;
        ++m_timesDowned;

        // Banked, not called: this runs inside YouGotHit, on the engine's stack,
        // halfway through its own damage handling.
        m_ragdollPending = m_settings.ragdollWhenDown;
    }

    void DownedState::ServiceRagdoll()
    {
        if (!m_armedFor)
        {
            m_ragdollPending = false;
            m_standUpPending = false;
            m_ragdollActive  = false;

            return;
        }

        auto* character = static_cast<ZHM5BaseCharacter*>(m_armedFor);

        if (m_ragdollPending)
        {
            m_ragdollPending = false;

            // What the engine does to the player on a real death: physics takes
            // the body and it falls where it stood. Every one of these is a
            // virtual the SDK already declares, so this needs no address.
            character->ActivateRagdoll();

            m_ragdollActive = true;
        }

        if (m_standUpPending)
        {
            m_standUpPending = false;

            if (m_ragdollActive)
            {
                // Back under animation, blended rather than snapped. Order
                // matters: ask for animation first, then let the ragdoll go, or
                // there is a frame with nothing driving the character at all.
                character->RequestAnimationDriven();
                character->DeactivatePoweredRagdoll(0.4f, true);
                character->ReleaseRagdoll();

                m_ragdollActive = false;
            }
        }
    }

    void DownedState::Update(float deltaSeconds, bool engineReportsDead)
    {
        if (!m_settings.enabled)
        {
            return;
        }

        // Whatever going down or getting up asked the character to do, done
        // here rather than where it was asked for.
        ServiceRagdoll();

        // The backstop checkbox is in the panel, so it can change between
        // arming and now. Refusals are remembered, so this does not re-decide
        // an unwritable flag every frame.
        if (m_armed && !m_immunityRefused && m_settings.alsoUseEngineImmunity != m_immunityApplied)
        {
            ApplyEngineImmunity(m_settings.alsoUseEngineImmunity);
        }

        // The second signal. Falls, drowning and scripted kills very likely
        // never reach YouGotHit, so if the engine says dead in spite of
        // everything above, that is a death and it is treated as one.
        if (engineReportsDead && m_phase == DownedPhase::Alive)
        {
            m_hitPoints = 0.f;
            GoDown();
        }

        if (m_phase == DownedPhase::Downed)
        {
            m_downedSeconds += deltaSeconds;

            // The way back that does not depend on anybody else. Without it,
            // playing alone or going down as a group is a state with no exit.
            if (m_settings.selfReviveSeconds > 0.f &&
                m_downedSeconds >= m_settings.selfReviveSeconds)
            {
                ReviveOnSceneChange();
            }

            return;
        }

        // Getting back up. This used to be where the state machine ended: the
        // phase was set to Recovering and nothing ever set it back, so the
        // first revive left the player permanently unable to take damage or go
        // down again. It is a window now, and the way out of it is here.
        if (m_phase == DownedPhase::Recovering)
        {
            m_recoveringSeconds += deltaSeconds;

            if (m_recoveringSeconds >= m_settings.recoveryGraceSeconds)
            {
                m_phase             = DownedPhase::Alive;
                m_recoveringSeconds = 0.f;
                m_sinceLastHit      = 0.f;
            }

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

        m_phase             = DownedPhase::Recovering;
        m_hitPoints         = m_settings.maxHitPoints;
        m_sinceLastHit      = 0.f;
        m_downedSeconds     = 0.f;
        m_recoveringSeconds = 0.f;

        // Cancel a collapse that has not happened yet, and undo one that has.
        m_ragdollPending = false;
        m_standUpPending = m_ragdollActive;
    }

    void DownedState::ReviveOnCheckpoint()
    {
        ReviveOnSceneChange();
    }
}
