#include <algorithm>
#include <cmath>

#include <Glacier/Engine/ZApplicationEngineWin32.h>
#include <Glacier/Engine/ZEngineAppCommon.h>
#include <Glacier/Render/ZRenderManager.h>
#include <Glacier/Render/IRenderDestinationEntity.h>
#include <Glacier/Camera/ZCameraEntity.h>
#include <Glacier/Player/ZHitman5.h>
#include <Glacier/Input/ZHM5InputControl.h>
#include <Glacier/ZLevelManager.h>
#include <Glacier/Math/SMatrix.h>
#include <Global.h>

#include "Game/Spectator.h"

namespace Coop::Game
{
    namespace
    {
        constexpr float kMinPitch    = -1.35f;
        constexpr float kMaxPitch    = 1.35f;
        constexpr float kMinDistance = 2.f;
        constexpr float kMaxDistance = 25.f;

        // How quickly the camera's look-at point chases the target. Low enough
        // that a teammate sprinting round a corner does not whip the view.
        constexpr float kTargetFollowPerSecond = 6.f;
    }

    Spectator::~Spectator()
    {
        Leave();
    }

    void Spectator::Enter()
    {
        if (m_active)
        {
            return;
        }

        ZApplicationEngineWin32* application = ZApplicationEngineWin32::GetInstance();

        if (!application || !RenderManager)
        {
            return;
        }

        ZEngineAppCommon& engineAppCommon = application->GetEngineAppCommon();

        if (!engineAppCommon.GetFreeCamera().GetRawPointer())
        {
            engineAppCommon.CreateFreeCameraAndControl();
        }

        TEntityRef<ZCameraEntity> freeCamera = engineAppCommon.GetFreeCamera();

        if (!freeCamera.GetRawPointer())
        {
            return;
        }

        TEntityRef<IRenderDestinationEntity> destination =
            RenderManager->GetGameRenderDestinationEntity();

        if (!destination.GetRawPointer())
        {
            return;
        }

        // Remember whatever was rendering, so Leave() can put it back rather
        // than assuming it was the player camera.
        TEntityRef<ZCameraEntity> previous = destination.GetRawPointer()->GetSource();

        engineAppCommon.SetMainCamera(previous);
        engineAppCommon.CopyMainCameraSettingsToFreeCamera();
        destination.GetRawPointer()->SetSource(freeCamera.GetEntityRef());

        // The control entity stays inactive: it exists so the engine has
        // somewhere to keep free-camera state, not so it can drive the view.
        if (ZFreeCameraControlEntity* control = engineAppCommon.GetFreeCameraControl().GetRawPointer())
        {
            control->SetActive(false);
        }

        // A downed player must not still be steering a body. DisableBindings
        // is counted rather than a flag, so this nests correctly with any
        // other mod doing the same thing - as long as the Leave() below runs.
        if (ZHitman5* hitman = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr)
        {
            if (ZHM5InputControl* inputControl = hitman->GetInputControl())
            {
                inputControl->DisableBindings();

                m_bindingsDisabledFor = hitman;
            }
        }

        m_hasSmoothedTarget = false;
        m_active            = true;
    }

    void Spectator::Leave()
    {
        if (!m_active)
        {
            return;
        }

        // Cleared first. Everything below can fail during a scene teardown,
        // and a Spectator that believes it is still active would try again
        // next frame against entities that have gone.
        m_active = false;

        ZApplicationEngineWin32* application = ZApplicationEngineWin32::GetInstance();

        if (!application || !RenderManager)
        {
            return;
        }

        ZEngineAppCommon& engineAppCommon = application->GetEngineAppCommon();

        TEntityRef<IRenderDestinationEntity> destination =
            RenderManager->GetGameRenderDestinationEntity();

        if (destination.GetRawPointer())
        {
            destination.GetRawPointer()->SetSource(engineAppCommon.GetMainCamera().GetEntityRef());
        }

        // Only undo a disable this camera actually did, and only on the
        // object it did it to. A scene change replaces the player; the new
        // one's input was never touched, and the old one's counter died with
        // it.
        if (ZHitman5* hitman = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr)
        {
            if (m_bindingsDisabledFor == hitman)
            {
                if (ZHM5InputControl* inputControl = hitman->GetInputControl())
                {
                    inputControl->EnableBindings();
                }
            }
        }

        m_bindingsDisabledFor = nullptr;
    }

    void Spectator::Nudge(float yawDelta, float pitchDelta, float distanceDelta)
    {
        m_yaw += yawDelta;
        m_pitch = std::clamp(m_pitch + pitchDelta, kMinPitch, kMaxPitch);
        m_distance = std::clamp(m_distance + distanceDelta, kMinDistance, kMaxDistance);
    }

    void Spectator::Update(float deltaSeconds, const SVector3& target, bool hasTarget)
    {
        if (!m_active || !hasTarget)
        {
            return;
        }

        ZApplicationEngineWin32* application = ZApplicationEngineWin32::GetInstance();

        if (!application)
        {
            return;
        }

        ZCameraEntity* camera = application->GetEngineAppCommon().GetFreeCamera().GetRawPointer();

        if (!camera)
        {
            return;
        }

        if (!m_hasSmoothedTarget)
        {
            m_smoothedTarget    = target;
            m_hasSmoothedTarget = true;
        }
        else
        {
            const float t = std::clamp(kTargetFollowPerSecond * deltaSeconds, 0.f, 1.f);

            m_smoothedTarget.x += (target.x - m_smoothedTarget.x) * t;
            m_smoothedTarget.y += (target.y - m_smoothedTarget.y) * t;
            m_smoothedTarget.z += (target.z - m_smoothedTarget.z) * t;
        }

        // World is Z-up. Orbit position, then a basis that looks back at the
        // target.
        const float cosPitch = std::cos(m_pitch);

        const SVector3 eye(
            m_smoothedTarget.x - std::cos(m_yaw) * cosPitch * m_distance,
            m_smoothedTarget.y - std::sin(m_yaw) * cosPitch * m_distance,
            m_smoothedTarget.z + std::sin(m_pitch) * m_distance + 1.2f);

        SVector3 forward(
            m_smoothedTarget.x - eye.x,
            m_smoothedTarget.y - eye.y,
            (m_smoothedTarget.z + 1.2f) - eye.z);

        const float length = std::sqrt(forward.x * forward.x +
                                       forward.y * forward.y +
                                       forward.z * forward.z);

        if (length < 0.0001f)
        {
            return;
        }

        forward.x /= length;
        forward.y /= length;
        forward.z /= length;

        // right = forward x worldUp, then up = right x forward. Both
        // normalised; the cross with (0,0,1) degenerates only when looking
        // straight down, which the pitch clamp prevents.
        SVector3 right(forward.y, -forward.x, 0.f);

        const float rightLength = std::sqrt(right.x * right.x + right.y * right.y);

        if (rightLength < 0.0001f)
        {
            return;
        }

        right.x /= rightLength;
        right.y /= rightLength;

        const SVector3 up(
            right.y * forward.z - right.z * forward.y,
            right.z * forward.x - right.x * forward.z,
            right.x * forward.y - right.y * forward.x);

        // The engine's convention, taken from the SDK's own noclip: forward is
        // -ZAxis, right is XAxis, up is YAxis. SMatrix's Left/Backward/Up
        // aliases do not mean what they say - Left is XAxis and points right -
        // so the axes are written directly.
        SMatrix transform;

        transform.XAxis = float4(right.x, right.y, right.z, 0.f);
        transform.YAxis = float4(up.x, up.y, up.z, 0.f);
        transform.ZAxis = float4(-forward.x, -forward.y, -forward.z, 0.f);
        transform.Trans = float4(eye.x, eye.y, eye.z, 1.f);

        camera->SetObjectToWorldMatrix(transform);
    }
}
