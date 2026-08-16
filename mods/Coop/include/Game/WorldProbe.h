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

        // Where the local player is, for anything that needs world space.
        const float4& PlayerPosition() const { return m_position; }

        uint32_t AliveActorCount() const { return m_aliveActorCount; }

    private:
        void UpdatePlayer(const SGameUpdateEvent& updateEvent);
        void UpdateActors();

        Net::StateMessage m_state;
        float4            m_position{};
        float4            m_previousPosition{};
        bool              m_hasPlayer          = false;
        bool              m_hasPreviousPosition = false;
        int               m_currentJumpPoint   = -1;
        uint32_t          m_aliveActorCount    = 0;

        // Name -> was alive last frame. Only ever populated from pointers read
        // in the current frame; nothing is stored that would be dereferenced
        // later. Actor pointers do not survive a scene reload.
        std::unordered_map<std::string, bool> m_actorAliveByName;

        std::vector<ActorDeath> m_pendingDeaths;
    };
}
