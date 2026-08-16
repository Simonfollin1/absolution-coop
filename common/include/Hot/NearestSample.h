#pragma once

#include "Hot/RouteCull.h"

// Finding the point on a reference route nearest to where the player is
// standing - the search behind the time delta, and the other loop in this mod
// that walks thousands of samples.
//
// Here for the same reason as RouteCull: so the bench measures the shipped code
// rather than a copy of it.
namespace Hot
{
    struct NearestResult
    {
        int   index      = 0;
        float distanceSq = 0.f;
        bool  found      = false;

        // True when the windowed search was thrown away and the whole route
        // scanned instead. Worth reporting rather than hiding: it is the
        // difference between a few hundred comparisons and tens of thousands,
        // and a route that triggers it every frame is a route whose window is
        // wrong.
        bool  fullScan   = false;
    };

    // hint is where the nearest sample was last time. The player moves
    // continuously, so this frame's answer is next to last frame's, and a window
    // around the hint replaces a scan of the whole route.
    //
    // A match against the edge of the window means the real nearest point is
    // probably outside it - a checkpoint warp, a reset, or a route that doubles
    // back - and those frames pay for the full scan rather than reporting a
    // delta measured against the wrong part of the route.
    NearestResult FindNearestSample(const RouteSample* samples, int count,
                                    const Point& position, int hint, int window);
}
