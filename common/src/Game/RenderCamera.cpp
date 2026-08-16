#include "Game/RenderCamera.h"

#include <cmath>

#include <Global.h>
#include <Glacier/Camera/ZCameraEntity.h>
#include <Glacier/Math/SMatrix.h>
#include <Glacier/Render/ZRenderManager.h>
#include <Glacier/Render/IRenderDestinationEntity.h>
#include <Glacier/Templates/TEntityRef.h>

Game::CameraView Game::GetRenderCamera()
{
    CameraView view;

    if (!RenderManager)
    {
        return view;
    }

    TEntityRef<IRenderDestinationEntity> destination =
        RenderManager->GetGameRenderDestinationEntity();

    IRenderDestinationEntity* destinationPtr = destination.GetRawPointer();

    if (!destinationPtr)
    {
        return view;
    }

    TEntityRef<ZCameraEntity> source = destinationPtr->GetSource();
    ZCameraEntity* camera = source.GetRawPointer();

    if (!camera)
    {
        return view;
    }

    const SMatrix cameraToWorld = camera->GetObjectToWorldMatrix();

    view.position = cameraToWorld.Trans;
    view.forward  = float4(
        -cameraToWorld.ZAxis.x,
        -cameraToWorld.ZAxis.y,
        -cameraToWorld.ZAxis.z,
        0.f);

    // A camera whose matrix has not been filled in yet reads as all zeroes, and
    // a zero-length forward would make every depth test come out as exactly on
    // the camera plane. Report that as no camera rather than as a strange one.
    const float lengthSquared =
        view.forward.x * view.forward.x +
        view.forward.y * view.forward.y +
        view.forward.z * view.forward.z;

    if (lengthSquared < 0.0001f)
    {
        return view;
    }

    const float inverseLength = 1.f / std::sqrt(lengthSquared);

    view.forward.x *= inverseLength;
    view.forward.y *= inverseLength;
    view.forward.z *= inverseLength;

    view.valid = true;

    return view;
}
