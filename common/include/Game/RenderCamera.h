#pragma once

#include <Glacier/Math/float4.h>

namespace Game
{
    // Where the game is drawing from this frame.
    struct CameraView
    {
        float4 position{};
        float4 forward{};   // unit length, points into the scene
        bool   valid = false;
    };

    // Reads the camera the frame is actually being rendered through, rather
    // than the player's own.
    //
    // Those are not the same thing and the difference matters: in a free or
    // cinematic camera the player stays behind while the view flies off, so
    // anything culled against the player would vanish from the shot. Asking the
    // render destination for its source answers the question directly - it is
    // whichever camera is bound right now.
    //
    // The convention is the one in docs/ENGINE-NOTES.md: Trans is the position
    // and forward is -ZAxis.
    CameraView GetRenderCamera();

    // Squared distance, for culling. Comparing squares avoids a square root per
    // point, which is worth having when the caller is walking a route with
    // thousands of them.
    inline float DistanceSquared(const float4& a, const float4& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;

        return dx * dx + dy * dy + dz * dz;
    }

    // How far in front of the camera a point is, in metres. Negative is behind
    // it, which is the cheapest useful rejection there is: no projection, no
    // matrix, one dot product.
    inline float DepthAlongView(const CameraView& view, const float4& point)
    {
        return (point.x - view.position.x) * view.forward.x +
               (point.y - view.position.y) * view.forward.y +
               (point.z - view.position.z) * view.forward.z;
    }
}
