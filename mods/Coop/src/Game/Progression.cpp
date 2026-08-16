#include <format>

#include "Game/Progression.h"

namespace Coop::Game
{
    namespace
    {
        // Between dying and the engine putting the player back there is a
        // scene load. Longer than any of them, short enough that a genuine
        // level change is not still waiting on it.
        constexpr float kDeathResolveTimeoutSeconds = 45.f;
    }

    void Progression::ResetForNewSession()
    {
        m_pending           = PendingJump{};
        m_localJumpPoint    = -1;
        m_localLevel        = 0xFF;
        m_teamJumpPoint     = -1;
        m_teamLevel         = 0xFF;
        m_sawDeadFlag       = false;
        m_playerWasPresent  = false;
        m_secondsSinceDeath = 0.f;
        m_deathPending      = false;
    }

    bool Progression::LocalIsBehindTeam() const
    {
        return m_teamLevel == m_localLevel
            && m_teamJumpPoint > m_localJumpPoint
            && m_localJumpPoint >= 0;
    }

    bool Progression::ObserveLocal(int jumpPoint, uint8_t level, bool playerPresent,
                                   bool playerDead, Net::EventMessage& outEvent)
    {
        bool produced = false;

        // Signal one: the engine says the player is dead. This can be missed
        // entirely if the death sequence is shorter than the gap between two
        // samples, which is why there is a signal two.
        if (playerDead && !m_sawDeadFlag)
        {
            m_sawDeadFlag       = true;
            m_deathPending      = true;
            m_secondsSinceDeath = 0.f;

            outEvent.type = Net::EventType::ObjectiveNote;
            outEvent.text = "died";
            produced      = true;
        }

        if (!playerDead)
        {
            m_sawDeadFlag = false;
        }

        // Signal two: the player entity went away and came back with the same
        // checkpoint. An ordinary level change moves the checkpoint, so this
        // separates "I died and reloaded" from "I finished the section".
        const bool cameBack = playerPresent && !m_playerWasPresent;

        m_playerWasPresent = playerPresent;

        if (jumpPoint >= 0 && level != 0xFF)
        {
            const bool movedForward = level != m_localLevel || jumpPoint > m_localJumpPoint;

            if (movedForward && m_localJumpPoint >= 0 && !m_deathPending)
            {
                outEvent.type = Net::EventType::CheckpointReached;
                outEvent.x    = static_cast<float>(jumpPoint);
                outEvent.y    = static_cast<float>(level);
                outEvent.text = std::format("checkpoint {}", jumpPoint);
                produced      = true;
            }

            // Coming back at the same place we died is the confirmation that
            // it was a death and not a transition.
            if (cameBack && m_deathPending && jumpPoint <= m_localJumpPoint)
            {
                m_deathPending = false;

                if (m_settings.deathPolicy == DeathPolicy::CatchUp && LocalIsBehindTeam())
                {
                    SchedulePull(m_teamJumpPoint, m_teamLevel,
                                 "Catching up to the team", false);
                }
            }

            m_localJumpPoint = jumpPoint;
            m_localLevel     = level;

            // Keep the team's high-water mark. Chapters only ever advance in
            // the campaign, so landing in a different one counts as forward.
            const bool teamUnknown      = m_teamLevel == 0xFF;
            const bool furtherSameLevel = level == m_teamLevel && jumpPoint > m_teamJumpPoint;
            const bool differentLevel   = m_teamLevel != 0xFF && level != m_teamLevel;

            if (teamUnknown || furtherSameLevel || differentLevel)
            {
                m_teamJumpPoint = jumpPoint;
                m_teamLevel     = level;
            }
        }

        return produced;
    }

    void Progression::ObserveRemote(const Net::EventMessage& event, const std::string& peerName)
    {
        if (event.type == Net::EventType::CheckpointReached)
        {
            const int     remoteJump  = static_cast<int>(event.x);
            const uint8_t remoteLevel = static_cast<uint8_t>(event.y);

            if (remoteJump < 0)
            {
                return;
            }

            // Track the furthest point anyone has reached, so a player who
            // dies later knows where to rejoin.
            if (remoteLevel != m_teamLevel || remoteJump > m_teamJumpPoint)
            {
                m_teamJumpPoint = remoteJump;
                m_teamLevel     = remoteLevel;
            }

            if (m_settings.followPolicy == FollowPolicy::Off)
            {
                return;
            }

            // Forward only, and never across a chapter boundary unannounced.
            // Being yanked backwards would delete progress the player made,
            // which is worse than being out of step for a while.
            if (m_localJumpPoint >= 0 && remoteLevel == m_localLevel && remoteJump > m_localJumpPoint)
            {
                SchedulePull(remoteJump, remoteLevel,
                             std::format("{} opened the way ahead", peerName), false);
            }

            return;
        }

        if (event.type == Net::EventType::ObjectiveNote && event.text == "died")
        {
            if (m_settings.deathPolicy == DeathPolicy::SharedFate && m_localJumpPoint >= 0)
            {
                // bResetHitman: everyone starts the section clean, which is the
                // point of sharing the fate.
                SchedulePull(m_localJumpPoint, m_localLevel,
                             std::format("{} died - restarting together", peerName), true);
            }
        }
    }

    void Progression::SchedulePull(int jumpPoint, uint8_t level,
                                   const std::string& reason, bool resetHitman)
    {
        // A pull that is already going somewhere at least as far ahead stands.
        if (m_pending.active && m_pending.level == level && m_pending.jumpPoint >= jumpPoint)
        {
            return;
        }

        m_pending.active      = true;
        m_pending.jumpPoint   = jumpPoint;
        m_pending.level       = level;
        m_pending.reason      = reason;
        m_pending.resetHitman = resetHitman;
        m_pending.remaining   = m_settings.followPolicy == FollowPolicy::AutoFollow
            ? m_settings.followGraceSeconds
            : 0.f;
    }

    bool Progression::Update(float deltaSeconds, int& outJumpPoint, bool& outResetHitman)
    {
        if (m_deathPending)
        {
            m_secondsSinceDeath += deltaSeconds;

            // Never leave a death unresolved forever - a missed "came back"
            // would otherwise suppress every checkpoint event from here on.
            if (m_secondsSinceDeath > kDeathResolveTimeoutSeconds)
            {
                m_deathPending = false;
            }
        }

        if (!m_pending.active)
        {
            return false;
        }

        // Two ways a pull becomes live: the grace window ran out under
        // AutoFollow, or the player said yes. Confirming has to work under
        // every policy, including Prompt, where no countdown ever runs.
        if (m_pendingConfirmed)
        {
            m_pending.remaining = 0.f;
        }
        else if (m_settings.followPolicy == FollowPolicy::AutoFollow)
        {
            m_pending.remaining -= deltaSeconds;
        }
        else
        {
            return false;
        }

        if (m_pending.remaining > 0.f)
        {
            return false;
        }

        outJumpPoint   = m_pending.jumpPoint;
        outResetHitman = m_pending.resetHitman;

        m_pending          = PendingJump{};
        m_pendingConfirmed = false;

        return true;
    }

    void Progression::ConfirmPending()
    {
        if (m_pending.active)
        {
            m_pendingConfirmed = true;
        }
    }

    void Progression::CancelPending()
    {
        m_pending          = PendingJump{};
        m_pendingConfirmed = false;
    }
}
