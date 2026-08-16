#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Net/Protocol.h"

namespace Coop::Game
{
    // What happens to the rest of the session when one player dies.
    enum class DeathPolicy : uint8_t
    {
        // Everyone reloads the checkpoint together. A blown run is a blown run.
        SharedFate = 0,

        // The dead player reloads alone; everyone else carries on. Cheapest,
        // and lets the worlds drift furthest apart.
        FreeRespawn = 1,

        // The default. The engine reloads the dead player at their own last
        // checkpoint, as it always does - and then this pulls them forward to
        // wherever the team has got to. Dying costs you the section you were
        // in, not the session.
        //
        // Deliberately needs no hook on the death itself, which is the one
        // thing we do not have an address for.
        CatchUp = 2,
    };

    // How a player follows the rest of the session through the level.
    enum class FollowPolicy : uint8_t
    {
        // Whoever gets there first drags everyone with them, after a grace
        // window long enough to finish what you were doing.
        AutoFollow = 0,

        // Same, but you press the key yourself.
        Prompt = 1,

        // Never move me. The session still reports where everyone is.
        Off = 2,
    };

    struct ProgressionSettings
    {
        DeathPolicy  deathPolicy  = DeathPolicy::CatchUp;
        FollowPolicy followPolicy = FollowPolicy::AutoFollow;

        // Seconds between "someone reached the next checkpoint" and being
        // pulled there. Long enough to finish a fight, short enough that
        // nobody is left standing around.
        float followGraceSeconds = 6.f;
    };

    // A pull that has been announced but not yet acted on.
    struct PendingJump
    {
        bool        active     = false;
        int         jumpPoint  = -1;
        uint8_t     level      = 0xFF;
        std::string reason;      // shown to the player
        float       remaining   = 0.f;
        bool        resetHitman = false;
    };

    // Decides when this client should move, given what the rest of the session
    // is doing. Owns no engine state and does no I/O: it takes observations
    // and produces intents, which is what makes it the one part of co-op that
    // can be reasoned about without the game running.
    class Progression
    {
    public:
        void SetSettings(const ProgressionSettings& settings) { m_settings = settings; }
        const ProgressionSettings& Settings() const { return m_settings; }

        // Called every frame with what the local world currently reports.
        // Appends whatever this frame produced.
        //
        // A frame can produce two: dying and crossing a checkpoint are read
        // from different signals and nothing stops them landing together. The
        // first version handed back one event by reference, so when that
        // happened the death note was silently overwritten by the checkpoint
        // and the other players never heard about it.
        void ObserveLocal(int jumpPoint, uint8_t level, bool playerPresent, bool playerDead,
                          std::vector<Net::EventMessage>& outEvents);

        // A CheckpointReached or PlayerDied arriving from another player.
        void ObserveRemote(const Net::EventMessage& event, const std::string& peerName);

        // Counts down the grace window. Returns true when the pull should
        // happen now, filling in what to jump to.
        bool Update(float deltaSeconds, int& outJumpPoint, bool& outResetHitman);

        const PendingJump& Pending() const { return m_pending; }

        // The player pressed the follow key, or dismissed the prompt.
        void ConfirmPending();
        void CancelPending();

        // Highest checkpoint anyone in the session has reached, which is where
        // CatchUp sends a dead player.
        int  TeamJumpPoint() const { return m_teamJumpPoint; }
        void ResetForNewSession();

        bool LocalIsBehindTeam() const;

    private:
        void SchedulePull(int jumpPoint, uint8_t level, const std::string& reason, bool resetHitman);

        ProgressionSettings m_settings;
        PendingJump         m_pending;

        int     m_localJumpPoint = -1;
        uint8_t m_localLevel     = 0xFF;
        int     m_teamJumpPoint  = -1;
        uint8_t m_teamLevel      = 0xFF;

        // Death detection needs two signals because neither is reliable alone.
        // The dead flag may not survive long enough to be sampled, and the
        // player entity going away also happens on an ordinary level change.
        bool  m_sawDeadFlag        = false;
        bool  m_playerWasPresent   = false;
        float m_secondsSinceDeath  = 0.f;
        bool  m_deathPending       = false;
        bool  m_pendingConfirmed   = false;
    };
}
