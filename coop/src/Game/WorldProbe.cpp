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
#include "Game/BuildInfo.h"

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

        const BuildInfo& build = BuildInfo::Get();

        m_state.level   = build.CurrentLevel();
        m_state.section = build.CurrentSection();

        if (build.IsLoading())
        {
            m_state.flags |= Net::SF_Loading;
        }
        else
        {
            m_state.flags &= static_cast<uint16_t>(~Net::SF_Loading);
        }

        if (build.IsInMenu())
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

        const float delta = updateEvent.m_RealTimeDelta;

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
        m_aliveActorCount = 0;

        if (!ActorManager)
        {
            return;
        }

        TArrayRef<TEntityRef<ZActor>> actors = ActorManager->GetAliveActors();

        m_aliveActorCount = static_cast<uint32_t>(actors.Size());

        // The rule that makes this safe: only pointers taken from *this*
        // frame's list are ever dereferenced. Actor pointers do not survive a
        // scene reload, so nothing is kept but names.
        std::unordered_map<std::string, bool> current;
        current.reserve(actors.Size());

        for (size_t i = 0; i < actors.Size(); ++i)
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

            current[name] = !dead;

            if (!dead)
            {
                continue;
            }

            const auto previous = m_actorAliveByName.find(name);

            // Alive last frame, dead now. Names are not guaranteed unique in a
            // scene, so this is a feed, not a ledger.
            if (previous != m_actorAliveByName.end() && previous->second)
            {
                ActorDeath death;
                death.name     = name;
                death.position = actor->GetWorldPosition();

                m_pendingDeaths.push_back(std::move(death));
            }
        }

        m_actorAliveByName = std::move(current);
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
