#pragma once

#include <cstdint>

#include <Glacier/Math/SVector3.h>

namespace Coop::Game
{
    // Where the camera goes while a player is down.
    //
    // Taking the camera over is the SDK's own documented sequence: create the
    // free camera, point the render destination at it, leave its control
    // entity inactive so the engine stops driving it, and write the transform
    // yourself each frame. Leaving the control entity inactive is what keeps
    // this off ZFreeCameraControlEntity::UpdateCamera, which the SDK's own
    // FreeCamera mod hooks and fully replaces - so the two do not fight.
    //
    // The camera orbits a target rather than being flown, because a downed
    // player should be watching their team, not exploring.
    class Spectator
    {
    public:
        ~Spectator();

        bool IsActive() const { return m_active; }

        // No-ops when already in the requested state, so both are safe to call
        // every frame from a state machine that does not track transitions.
        void Enter();
        void Leave();

        // Called each frame while active. target is who we are watching -
        // usually a teammate, and the player's own body when there is nobody
        // else in the level.
        void Update(float deltaSeconds, const SVector3& target, bool hasTarget);

        // Orbit controls, driven by whatever the mod binds to them.
        void Nudge(float yawDelta, float pitchDelta, float distanceDelta);

    private:
        // The player whose input Enter() switched off. Leave() must only
        // switch input back on for that same object: after a scene change the
        // new player never had DisableBindings called, and enabling it there
        // unbalances a counter this mod never touched.
        void* m_bindingsDisabledFor = nullptr;

        bool  m_active   = false;
        float m_yaw      = 0.f;
        float m_pitch    = 0.35f;
        float m_distance = 6.f;

        SVector3 m_smoothedTarget{};
        bool     m_hasSmoothedTarget = false;
    };
}
