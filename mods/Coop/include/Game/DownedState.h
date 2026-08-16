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
        Recovering  = 2,  // just got up - briefly immune, then Alive again
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
        // falls, drowning, scripted kills. Sets the engine's own player
        // god-mode flag, the same one the SDK's Player mod exposes as a
        // checkbox, and puts back whatever was there when the mod lets go.
        //
        // What this replaced was a write to HitmanDamageReceivedMultiplier
        // through the configuration system, which did nothing at all: that name
        // is a per-difficulty array in HMA.ini's [Difficulty] section, not a
        // registered console variable. Enumerating all 1648 of them in the game
        // is what proved it - it is not among them.
        bool alsoUseEngineImmunity = true;

        // Getting up again on your own, after this long, when nothing else has
        // brought you back.
        //
        // Without it being down is a softlock. The other two ways back in are
        // a scene change and somebody else reaching a checkpoint, and neither
        // exists when you are playing alone or when the whole session is down
        // at once. Set to zero to rely on those only.
        float selfReviveSeconds = 20.f;

        // Going limp where you stand, rather than standing there at zero health
        // looking fine.
        //
        // ZHM5BaseCharacter declares ActivateRagdoll, RequestAnimationDriven
        // and DeactivatePoweredRagdoll as ordinary virtuals, so this is a call
        // through a vtable we already hold a pointer to - no address, no hook,
        // and the same on both builds. It is what the engine does to the player
        // on a real death, minus the death.
        bool ragdollWhenDown = true;

        // Getting out of the fight once you are in it.
        //
        // The engine is never told you went down - that is the whole mechanism
        // - so to the AI there is a live hostile lying on the floor, and they
        // keep shooting it. A session's log showed forty-odd hits landing on a
        // player who was already spectating.
        //
        // So the body leaves. It falls where it stood, lies there long enough
        // to read as a death, and is then hidden and moved out from under the
        // level; getting up puts it back exactly where it was. Nothing to
        // shoot at, and no engine state changed to achieve it.
        // Back on, and quick.
        //
        // Blinding the AI was tried and did not stop them: they carried on
        // shooting a player who was already down. Perception decides whether
        // they *find* a target, not whether they let one go - once a guard is
        // in combat it has you and keeps you, and no amount of switching off
        // its eyes changes that.
        //
        // Taking the body away does work, because there is then nothing to
        // shoot at. It is a blunter answer than making them believe you are
        // dead, and it is the one that holds up.
        bool  hideBodyWhenDown  = true;
        float hideAfterSeconds  = 1.2f;

        // How long after getting up hits are ignored.
        //
        // Standing up inside the firefight that put you down and being dropped
        // again in the same second is not a mechanic, it is a loop. This is the
        // window to get behind something.
        float recoveryGraceSeconds = 3.f;
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

        // How long the player has been down, and how long until they get up
        // by themselves. Zero when not down, or when self-revive is off.
        float DownedSeconds() const { return m_downedSeconds; }
        float SecondsUntilSelfRevive() const;

        // How much of the post-revive grace window is left. Zero unless we are
        // in it, which makes it safe to drive a HUD line straight off.
        float SecondsOfGraceLeft() const;

        // Where the body went down. The spectator camera wants this rather
        // than the player's live position, which is under the level once the
        // body has been taken out of the fight.
        bool  HaveDownPosition() const { return m_haveDownPos; }
        float DownX() const { return m_downX; }
        float DownY() const { return m_downY; }
        float DownZ() const { return m_downZ; }

        bool BodyHidden() const { return m_bodyHidden; }

        const std::string& Diagnostic() const { return m_diagnostic; }

        // What the engine-level backstop actually managed to do, which is not
        // the same question as whether the vtable patch armed.
        const std::string& ImmunityNote() const { return m_immunityNote; }

        // Called from the replacement vtable entry. Public because the thunk
        // is a free function; not part of the interface anyone else should use.
        void OnHitIntercepted(const void* hitInfo);

        // --- Research switch: letting a hit reach the engine ------------------
        //
        // The mod carries a shadow health pool because the engine's own is at
        // an offset nobody has found. The way to find it is to let exactly one
        // hit through and see what changed in the player object, which is a
        // thing only a running game can answer.
        //
        // Dangerous on purpose and never on by default: a hit that reaches the
        // engine can kill the player, and a death is a checkpoint reload. Arm
        // it, take one hit, read the diff.
        void ArmPassThrough(uint32_t hits);

        // True at most `hits` times after arming. Called from the thunk.
        bool ConsumePassThrough();

        uint32_t PassThroughRemaining() const { return m_passThroughRemaining; }

        // Calls the vtable entry we replaced. Only meaningful while armed.
        void CallOriginalYouGotHit(void* self, const void* hitInfo) const;

        // A copy of the player object's bytes, for diffing across a hit.
        // Returns how many bytes it managed to read.
        size_t SnapshotPlayer(uint8_t* destination, size_t count) const;

        void ResetForNewSession();

    private:
        bool ValidateAndPatch(void** vtable, size_t slot);
        void GoDown();
        void ApplyEngineImmunity(bool enable);

        // Spends whatever GoDown and ReviveOnSceneChange asked for, on the game
        // thread and outside the engine's own damage call.
        void ServiceRagdoll();

        void HideBody();
        void RestoreBody();

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
        float       m_downedSeconds = 0.f;

        // Time since standing up. Only meaningful in Recovering, which is a
        // state Update() counts down and then leaves - it used to be one
        // nothing ever left, which made the first revive permanent immunity.
        float       m_recoveringSeconds = 0.f;

        bool        m_armed          = false;
        void**      m_patchedVtable  = nullptr;
        size_t      m_patchedSlot    = 0;
        void*       m_originalEntry  = nullptr;
        ZHitman5*   m_armedFor       = nullptr;

        // Ragdolling is asked for inside OnHitIntercepted, which runs on the
        // engine's own stack halfway through its damage handling. Calling back
        // into the character from there is asking for trouble, so the request
        // is banked and spent in Update - one frame later, on the same thread,
        // with the engine's call long returned.
        bool m_ragdollPending = false;
        bool m_ragdollActive  = false;
        bool m_standUpPending = false;

        // Where the body went down, so it can be put back exactly there, and
        // so the spectator camera watches the spot rather than following the
        // body out of the world.
        bool  m_bodyHidden  = false;
        bool  m_haveDownPos = false;
        float m_downX = 0.f;
        float m_downY = 0.f;
        float m_downZ = 0.f;

        // Written from the UI thread, read and decremented on the game thread.
        // A hit landing on the frame it is armed or disarmed is not a race
        // worth a lock: the worst outcome is one hit either way, which is the
        // granularity of the thing anyway.
        uint32_t    m_passThroughRemaining = 0;

        // The engine's own god-mode flag, and what was in it before we wrote.
        // Refused means we looked and decided not to, which is a decision to
        // remember rather than one to re-make sixty times a second.
        bool        m_immunityApplied  = false;
        bool        m_immunityRefused  = false;
        int         m_immunityPrevious = 0;

        std::string m_diagnostic;
        std::string m_immunityNote;
    };

    // The one instance the vtable thunk dispatches to. There is exactly one
    // local player, so a single owner is the honest model.
    DownedState& TheDownedState();
}
