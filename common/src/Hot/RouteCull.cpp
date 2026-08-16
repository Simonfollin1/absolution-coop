#include "Hot/RouteCull.h"

namespace
{
    // Behind the camera counts as culled however far away it is, but a segment
    // straddling the camera plane still has to be drawn, so the test allows a
    // little slack rather than cutting exactly at zero.
    constexpr float BEHIND_SLACK = 4.f;

    int CeilDiv(int value, int by)
    {
        return by > 0 ? (value + by - 1) / by : 1;
    }

    int Max(int a, int b)
    {
        return a > b ? a : b;
    }

    float DistanceSquared(const Hot::Point& a, const Hot::RouteSample& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;

        return dx * dx + dy * dy + dz * dz;
    }

    float DepthAlongView(const Hot::View& view, const Hot::RouteSample& point)
    {
        return (point.x - view.position.x) * view.forward.x +
               (point.y - view.position.y) * view.forward.y +
               (point.z - view.position.z) * view.forward.z;
    }
}

Hot::CullResult Hot::SelectRouteSegments(const RouteSample* samples, int count,
                                         const View& view, const CullParams& params,
                                         int* outFirst, int outCapacity)
{
    CullResult result;

    if (!samples || !outFirst || count < 2 ||
        params.drawBudget <= 0 || params.examineBudget <= 0 || outCapacity <= 0)
    {
        return result;
    }

    const int total = count - 1;

    // Two reasons to step over points, and the loop obeys whichever is stricter.
    //
    // With no distance limit every segment is drawn, so the only way to stay
    // inside the draw budget is to draw fewer points. That is the original
    // thinning and it is unchanged.
    //
    // With a distance limit almost everything is culled instead, so the draw
    // budget is never reached and stops bounding anything - the loop used to run
    // to the end of the route regardless. The examine budget is what bounds it,
    // and it thins rather than truncates on purpose: stopping partway would
    // delete the far half of the route from the picture, where stepping over
    // points keeps the whole shape and only coarsens the line.
    const int drawStride    = params.limitDistance ? 1 : CeilDiv(total, params.drawBudget);
    const int examineStride = CeilDiv(total, params.examineBudget);
    const int stride        = Max(1, Max(drawStride, examineStride));

    result.stride = stride;

    const int limit = params.drawBudget < outCapacity ? params.drawBudget : outCapacity;

    // Two loops rather than one with a test in it. Whether the distance limit is
    // on is fixed for the whole route, so asking per segment is a branch the
    // caller has already answered - and with the limit off the test is most of
    // what the loop does.
    if (!params.limitDistance)
    {
        for (int i = stride; i <= total; i += stride)
        {
            ++result.examined;

            if (result.drawn >= limit)
            {
                result.hitBudget = true;
                break;
            }

            outFirst[result.drawn] = i - stride;

            ++result.drawn;
        }

        return result;
    }

    // Each segment's far endpoint is the next segment's near endpoint, so both
    // tests on it are carried forward rather than recomputed. That halves the
    // arithmetic in the loop, and it is exact rather than an approximation: the
    // same two floats, computed once instead of twice.
    float previousDistSq = DistanceSquared(view.position, samples[0]);
    float previousDepth  = DepthAlongView(view, samples[0]);

    for (int i = stride; i <= total; i += stride)
    {
        ++result.examined;

        const RouteSample& to = samples[i];

        const float fromDistSq = previousDistSq;
        const float fromDepth  = previousDepth;

        const float toDistSq = DistanceSquared(view.position, to);
        const float toDepth  = DepthAlongView(view, to);

        previousDistSq = toDistSq;
        previousDepth  = toDepth;

        const bool tooFar = fromDistSq > params.maxDistanceSq &&
                            toDistSq   > params.maxDistanceSq;

        const bool behind = fromDepth < -BEHIND_SLACK &&
                            toDepth   < -BEHIND_SLACK;

        if (tooFar || behind)
        {
            ++result.culled;
            continue;
        }

        if (result.drawn >= limit)
        {
            result.hitBudget = true;
            break;
        }

        outFirst[result.drawn] = i - stride;

        ++result.drawn;
    }

    return result;
}
