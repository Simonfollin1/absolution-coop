#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <Glacier/Math/float4.h>
#include <Glacier/SGameUpdateEvent.h>

#include "Net/Protocol.h"

namespace Coop::Game
{
    // What one death in the local world looks like to the rest of the session.
    struct ActorDeath
    {
        std::string name;
        float4      position{};

        // Two ways an actor stops being a live one: it is still listed and
        // IsDead now says so, or it left the alive list altogether. Which of
        // them the engine actually uses is not documented anywhere, so both are
        // taken and counted separately - see WorldProbe::DeathsByFlag.
        bool vanished = false;
    };

    // Reads the local player and the local world once per frame, and turns
    // that into something the session can send.
    //
    // Everything is read through the SDK's public accessors. No hooks, no new
    // addresses - which is the whole reason this half of co-op is buildable
    // today.
    class WorldProbe
    {
    public:
        void Update(const SGameUpdateEvent& updateEvent);

        bool HasPlayer() const { return m_hasPlayer; }

        // The local player as a wire message, ready to publish. peerId and
        // timestamp are filled in by the session.
        const Net::StateMessage& LocalState() const { return m_state; }

        // Checkpoint index, or -1 when it could not be read. This is the unit
        // the session synchronises on: "everyone jump to 12".
        int CurrentJumpPoint() const { return m_currentJumpPoint; }

        // Asks the engine to move the local player to a checkpoint. This is a
        // real level transition, not a teleport - the scene reloads.
        static bool JumpToCheckpoint(int index, bool resetHitman);

        // Deaths noticed since the last call. Drained, so each is reported once.
        std::vector<ActorDeath> DrainDeaths();

        // Drops everything remembered about the previous scene's actors.
        //
        // Without this a level change reads as every actor in the old scene
        // vanishing at once, and the feed fills with the deaths of people who
        // are merely somewhere else now.
        void ForgetActors();

        // Where the local player is, for anything that needs world space.
        const float4& PlayerPosition() const { return m_position; }

        uint32_t AliveActorCount() const { return m_aliveActorCount; }

        // How many actors in the list are currently flagged dead. If the engine
        // drops the dead from its own alive list this stays at zero forever,
        // which is the thing worth knowing.
        uint32_t DeadFlaggedCount() const { return m_deadFlaggedCount; }

        // Running totals since load, one per signal. Killing a guard and
        // watching which of these moves is how you tell what the engine does.
        uint32_t DeathsByFlag() const { return m_deathsByFlag; }
        uint32_t DeathsByVanish() const { return m_deathsByVanish; }

    private:
        void UpdatePlayer(const SGameUpdateEvent& updateEvent);
        void UpdateActors();

        // One actor as it looked last frame. The position is kept because an
        // actor that vanished cannot be asked where it was.
        struct Sighting
        {
            bool   alive = false;
            float4 position{};
        };

        Net::StateMessage m_state;
        float4            m_position{};
        float4            m_previousPosition{};
        bool              m_hasPlayer          = false;
        bool              m_hasPreviousPosition = false;
        int               m_currentJumpPoint   = -1;
        uint32_t          m_aliveActorCount    = 0;
        uint32_t          m_deadFlaggedCount   = 0;
        uint32_t          m_deathsByFlag       = 0;
        uint32_t          m_deathsByVanish     = 0;

        // Name -> how it looked last frame. Only ever populated from pointers
        // read in the current frame; nothing is stored that would be
        // dereferenced later. Actor pointers do not survive a scene reload.
        std::unordered_map<std::string, Sighting> m_actorsByName;

        std::vector<ActorDeath> m_pendingDeaths;
    };
}
