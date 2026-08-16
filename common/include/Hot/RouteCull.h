#pragma once

// The route drawing hot path, lifted out of RouteRecorder so it can be measured.
//
// Nothing in this header touches the SDK, the renderer or the game. That is the
// whole point: this file and its .cpp compile unchanged into the mod and into
// tools/bench, so a number from the bench is a number about the code that
// actually runs rather than about a re-implementation of it that has quietly
// drifted.
//
// The split is drawn where it costs nothing to draw it. Deciding which segments
// to draw is arithmetic over an array; submitting them is a renderer call. The
// first is what runs thousands of times a frame and the second is what needs a
// device, so the first is here and the second stays with the caller.
namespace Hot
{
    // One position sample along a recorded route. This is the storage format
    // RouteRecorder keeps, declared here rather than there so the bench can
    // build a route without pulling in Glacier headers.
    struct RouteSample
    {
        float  x = 0.f;
        float  y = 0.f;
        float  z = 0.f;
        double time = 0.0;  // seconds since recording started
    };

    struct Point
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    // The camera being rendered through, reduced to what culling needs.
    struct View
    {
        Point position;
        Point forward;   // unit length, points into the scene
    };

    struct CullParams
    {
        // Off means every segment is drawn and only the draw budget thins the
        // route. On means almost everything is culled instead, which is why the
        // examine budget exists - see the note on it in RouteRecorder.h.
        bool  limitDistance = false;
        float maxDistanceSq = 0.f;

        int   drawBudget    = 0;   // segments submitted to the renderer
        int   examineBudget = 0;   // segments the loop may look at
    };

    struct CullResult
    {
        int  drawn     = 0;
        int  culled    = 0;
        int  examined  = 0;
        int  stride    = 1;
        bool hitBudget = false;
    };

    // Picks the segments of one route that should be drawn.
    //
    // Writes the first endpoint index of each chosen segment into outFirst; the
    // second endpoint is that index plus CullResult::stride. The caller draws
    // them, which keeps every renderer type out of this translation unit.
    //
    // outCapacity bounds the writes independently of the budget so a caller
    // cannot overrun its buffer by moving a slider.
    CullResult SelectRouteSegments(const RouteSample* samples, int count,
                                   const View& view, const CullParams& params,
                                   int* outFirst, int outCapacity);
}
