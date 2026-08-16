#include <cmath>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Player/ZHitman5.h>
#include <Glacier/Actor/ZActorManager.h>
#include <Glacier/CheckPoint/ZCheckPointManager.h>
#include <Glacier/CheckPoint/ZCheckPointManagerEntity.h>
#include <Glacier/Render/ZSpatialEntity.h>
#include <Glacier/Math/SMatrix.h>
#include <Global.h>

#include "Game/WorldProbe.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    namespace
    {
        // Above this the player reads as running rather than walking. Chosen to
        // sit between Absolution's walk and run speeds with room either side,
        // because it only drives a label.
        constexpr float kRunningSpeed = 3.2f;

        // A frame long enough that velocity from it would be noise - a loading
        // screen, a breakpoint, a stall.
        constexpr float kMaxUsefulDelta = 0.25f;
    }

    void WorldProbe::Update(const SGameUpdateEvent& updateEvent)
    {
        UpdatePlayer(updateEvent);
        UpdateActors();

        const int level   = GameOffsets::GetLevel();
        const int section = GameOffsets::GetSection();

        m_state.level   = (GameOffsets::IsSupported() && level   >= 0 && level   <= 25)
                        ? static_cast<uint8_t>(level) : 0xFF;
        m_state.section = (GameOffsets::IsSupported() && section >= 0 && section <= 254)
                        ? static_cast<uint8_t>(section) : 0xFF;

        if (GameOffsets::IsLoading())
        {
            m_state.flags |= Net::SF_Loading;
        }
        else
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_Loading);
        }

        if (GameOffsets::IsInMenu())
        {
            m_state.flags |= Net::SF_InMenu;
        }
        else
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_InMenu);
        }

        m_currentJumpPoint = -1;

        if (CheckPointManager)
        {
            const auto managerEntity = CheckPointManager->GetCheckPointManagerEntity();

            if (managerEntity.GetRawPointer())
            {
                m_currentJumpPoint = managerEntity.GetRawPointer()->GetCurrentJumpPoint();
            }
        }
    }

    void WorldProbe::UpdatePlayer(const SGameUpdateEvent& updateEvent)
    {
        m_hasPlayer = false;

        if (!LevelManager)
        {
            return;
        }

        const TEntityRef<ZHitman5>& hitmanRef = LevelManager->GetHitman();
        ZHitman5* hitman = hitmanRef.GetRawPointer();

        if (!hitman)
        {
            // Between scenes there is no player. Clearing the position flag is
            // what stops peers from drawing a marker at wherever we were when
            // the level ended.
            m_state.flags &= static_cast<uint16_t>(~Net::SF_HasPosition);
            m_hasPreviousPosition = false;

            return;
        }

        ZSpatialEntity* spatial = hitman->GetSpatialEntityPtr();

        if (!spatial)
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_HasPosition);
            return;
        }

        const SMatrix transform = spatial->GetObjectToWorldMatrix();

        m_position = spatial->GetWorldPosition();

        m_state.x = m_position.x;
        m_state.y = m_position.y;
        m_state.z = m_position.z;

        // World is Z-up and forward is -ZAxis. Both of those are the SDK's own
        // convention, taken from its noclip and confirmed by the raycast
        // FreeCamera uses to place the player - SMatrix's Left/Backward/Up
        // aliases do not mean what they say and are not used here.
        const float forwardX = -transform.ZAxis.x;
        const float forwardY = -transform.ZAxis.y;

        m_state.yaw = std::atan2(forwardY, forwardX);

        // m_RealTimeDelta is a ZGameTime, not a float - it holds ticks and
        // only converts on request. Real time rather than game time, so this
        // keeps working while the game is paused.
        const float delta = static_cast<float>(updateEvent.m_RealTimeDelta.ToSeconds());

        if (m_hasPreviousPosition && delta > 0.f && delta < kMaxUsefulDelta)
        {
            m_state.vx = (m_position.x - m_previousPosition.x) / delta;
            m_state.vy = (m_position.y - m_previousPosition.y) / delta;
            m_state.vz = (m_position.z - m_previousPosition.z) / delta;
        }
        else
        {
            m_state.vx = 0.f;
            m_state.vy = 0.f;
            m_state.vz = 0.f;
        }

        m_previousPosition    = m_position;
        m_hasPreviousPosition = true;

        const float horizontalSpeed =
            std::sqrt(m_state.vx * m_state.vx + m_state.vy * m_state.vy);

        m_state.flags |= Net::SF_HasPosition;

        if (horizontalSpeed > kRunningSpeed)
        {
            m_state.flags |= Net::SF_Running;
        }
        else
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_Running);
        }

        if (hitman->IsDead())
        {
            m_state.flags |= Net::SF_Dead;
        }
        else
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_Dead);
        }

        m_hasPlayer = true;
    }

    void WorldProbe::UpdateActors()
    {
        m_aliveActorCount  = 0;
        m_deadFlaggedCount = 0;

        if (!ActorManager)
        {
            return;
        }

        TArrayRef<TEntityRef<ZActor>> actors = ActorManager->GetAliveActors();

        const size_t listSize = actors.Size();

        m_aliveActorCount = static_cast<uint32_t>(listSize);

        // The rule that makes this safe: only pointers taken from *this*
        // frame's list are ever dereferenced. Actor pointers do not survive a
        // scene reload, so nothing is kept but names and positions.
        std::unordered_map<std::string, Sighting> current;
        current.reserve(listSize);

        for (size_t i = 0; i < listSize; ++i)
        {
            ZActor* actor = actors[i].GetRawPointer();

            if (!actor)
            {
                continue;
            }

            const ZString& actorName = actor->GetActorName();
            const char*    text      = actorName.ToCString();

            if (!text)
            {
                continue;
            }

            std::string name(text);
            const bool  dead = actor->IsDead();

            Sighting sighting;
            sighting.alive    = !dead;
            sighting.position = actor->GetWorldPosition();

            const auto previous = m_actorsByName.find(name);
            const bool wasAlive = previous != m_actorsByName.end() && previous->second.alive;

            current[name] = sighting;

            if (!dead)
            {
                continue;
            }

            ++m_deadFlaggedCount;

            // Alive last frame, flagged dead now. Names are not guaranteed
            // unique in a scene, so this is a feed, not a ledger.
            if (wasAlive)
            {
                ActorDeath death;
                death.name     = name;
                death.position = sighting.position;

                m_pendingDeaths.push_back(std::move(death));
                ++m_deathsByFlag;
            }
        }

        // The other signal. The engine calls this list its *alive* actors, and
        // it may well drop an actor the moment it dies rather than leaving it
        // there with a flag set - in which case the loop above never fires and
        // nothing is ever reported.
        //
        // Guarded three ways, because leaving the list is also what streaming
        // out and a scene teardown look like: there has to be a player, the
        // list has to still hold roughly what it held before, and the actor has
        // to have been alive rather than already dead when we last saw it.
        const bool listIsStable = !m_actorsByName.empty()
                               && listSize > 0
                               && listSize + 4 >= m_actorsByName.size();

        if (m_hasPlayer && listIsStable)
        {
            for (const auto& [name, sighting] : m_actorsByName)
            {
                if (!sighting.alive || current.contains(name))
                {
                    continue;
                }

                ActorDeath death;
                death.name     = name;
                death.position = sighting.position;
                death.vanished = true;

                m_pendingDeaths.push_back(std::move(death));
                ++m_deathsByVanish;
            }
        }

        m_actorsByName = std::move(current);
    }

    void WorldProbe::ForgetActors()
    {
        m_actorsByName.clear();
        m_pendingDeaths.clear();

        m_deadFlaggedCount = 0;
    }

    std::vector<ActorDeath> WorldProbe::DrainDeaths()
    {
        std::vector<ActorDeath> result = std::move(m_pendingDeaths);

        m_pendingDeaths.clear();

        return result;
    }

    bool WorldProbe::JumpToCheckpoint(int index, bool resetHitman)
    {
        if (!CheckPointManager || index < 0)
        {
            return false;
        }

        const auto managerEntity = CheckPointManager->GetCheckPointManagerEntity();
        ZCheckPointManagerEntity* entity = managerEntity.GetRawPointer();

        if (!entity)
        {
            return false;
        }

        entity->ActivateJumpPoint(index, resetHitman);

        return true;
    }
}
