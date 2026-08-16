#pragma once

#include <cstdint>
#include <string>

class ZHitman5;

namespace Coop::Game
{
    // Co-op death, done without ever letting the engine kill the player.
    //
    // Absolution's own answer to dying is a death sequence and a checkpoint
    // reload. In a session where everybody runs their own copy of the level,
    // that reload resets one player's whole world while the others carry on -
    // it is the single most destructive thing that can happen to a shared run.
    //
    // So the engine never gets told. Damage is intercepted before it lands,
    // scored against a shadow pool this class owns, and when that pool runs
    // out the player goes to a spectator camera instead of to a loading
    // screen. Nothing about the level changes. The run continues.
    //
    // The interception is a vtable patch on the player instance rather than a
    // detour at a fixed address, which is what makes it work on Steam and GOG
    // alike and why it needs no reverse engineering.
    enum class DownedPhase : uint8_t
    {
        Alive       = 0,
        Downed      = 1,  // spectating, waiting for a way back in
        Recovering  = 2,  // a revive condition fired, restoring control
    };

    struct DownedSettings
    {
        bool  enabled = true;

        // Absolution's own default maximum, from ZHM5Health in the dev build's
        // headers. Mirroring it keeps the shadow pool feeling like the real one.
        float maxHitPoints = 100.f;

        // Out of combat the game regenerates. So does this.
        float regenPerSecond      = 12.f;
        float regenDelaySeconds   = 4.f;

        // A hit whose magnitude could not be read costs this much. Only used
        // when SHitInfo's damage cannot be recovered - see DamageFromHit.
        float fallbackDamagePerHit = 25.f;

        // Belt and braces for the damage paths that never reach YouGotHit -
        // falls, drowning, scripted kills. Scales the engine's own received-
        // damage multiplier to zero through the configuration system, which
        // needs no address and works the same on Steam and GOG.
        //
        // This replaced a write to a hardcoded god-mode address. That was
        // Steam-only, and the SDK's own two mods disagree about where it is:
        // Player says 0xD4F5E0, Actors says 0xD4D91C. Neither is confirmed,
        // and a wrong one is a write into whatever else lives there.
        bool alsoUseEngineImmunity = true;
    };

    class DownedState
    {
    public:
        ~DownedState();

        DownedSettings&       Settings() { return m_settings; }
        const DownedSettings& Settings() const { return m_settings; }

        // Patches the player's vtable. Safe to call every frame: it does
        // nothing once armed, and re-arms by itself when the player entity is
        // replaced on a scene change.
        //
        // Returns false when the hook could not be validated, in which case
        // nothing was patched and the engine keeps handling damage itself.
        bool Arm(ZHitman5* hitman);

        // Puts the original vtable entry back. Called on unload, and on any
        // scene teardown, because leaving a patched pointer behind after the
        // DLL has gone is a crash with our name on it and no stack to prove it.
        void Disarm();

        void Update(float deltaSeconds, bool engineReportsDead);

        // A scene loaded, or somebody in the session reached a checkpoint.
        // Either is a way back into the run.
        void ReviveOnSceneChange();
        void ReviveOnCheckpoint();

        DownedPhase Phase() const { return m_phase; }
        bool        IsDowned() const { return m_phase == DownedPhase::Downed; }
        float       HitPoints() const { return m_hitPoints; }
        float       HitPointsFraction() const;
        bool        IsArmed() const { return m_armed; }
        uint32_t    HitsTaken() const { return m_hitsTaken; }
        uint32_t    TimesDowned() const { return m_timesDowned; }

        const std::string& Diagnostic() const { return m_diagnostic; }

        // Called from the replacement vtable entry. Public because the thunk
        // is a free function; not part of the interface anyone else should use.
        void OnHitIntercepted(const void* hitInfo);

        void ResetForNewSession();

    private:
        bool ValidateAndPatch(void** vtable, size_t slot);
        void GoDown();
        void ApplyEngineImmunity(bool enable);

        // Reads a damage figure out of SHitInfo. The struct's layout is
        // published for the 2012 development build and has not been confirmed
        // against retail, so this is allowed to fail and say so rather than
        // report a number it cannot stand behind.
        bool DamageFromHit(const void* hitInfo, float& outDamage) const;

        DownedSettings m_settings;

        DownedPhase m_phase        = DownedPhase::Alive;
        float       m_hitPoints    = 100.f;
        float       m_sinceLastHit = 0.f;
        uint32_t    m_hitsTaken    = 0;
        uint32_t    m_timesDowned  = 0;

        bool        m_armed          = false;
        void**      m_patchedVtable  = nullptr;
        size_t      m_patchedSlot    = 0;
        void*       m_originalEntry  = nullptr;
        ZHitman5*   m_armedFor       = nullptr;

        std::string m_diagnostic;
    };

    // The one instance the vtable thunk dispatches to. There is exactly one
    // local player, so a single owner is the honest model.
    DownedState& TheDownedState();
}
