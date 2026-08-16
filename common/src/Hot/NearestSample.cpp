#include "Hot/NearestSample.h"

namespace
{
    bool Scan(const Hot::RouteSample* samples, int first, int last,
              const Hot::Point& position, int& bestIndexOut, float& bestDistanceOut)
    {
        bool found = false;

        for (int i = first; i < last; ++i)
        {
            const Hot::RouteSample& sample = samples[i];

            const float dx = position.x - sample.x;
            const float dy = position.y - sample.y;
            const float dz = position.z - sample.z;

            const float distance = dx * dx + dy * dy + dz * dz;

            if (!found || distance < bestDistanceOut)
            {
                bestDistanceOut = distance;
                bestIndexOut    = i;
                found           = true;
            }
        }

        return found;
    }
}

Hot::NearestResult Hot::FindNearestSample(const RouteSample* samples, int count,
                                          const Point& position, int hint, int window)
{
    NearestResult result;

    if (!samples || count <= 0 || window <= 0)
    {
        return result;
    }

    const int centre = hint < 0 ? 0 : (hint > count - 1 ? count - 1 : hint);
    const int first  = centre > window ? centre - window : 0;
    const int last   = centre + window < count ? centre + window : count;

    int   bestIndex    = 0;
    float bestDistance = 0.f;

    bool found = Scan(samples, first, last, position, bestIndex, bestDistance);

    const bool atEdge = found &&
        ((first > 0 && bestIndex < first + window / 8) ||
         (last < count && bestIndex + window / 8 >= last));

    if (!found || atEdge)
    {
        found = Scan(samples, 0, count, position, bestIndex, bestDistance);

        result.fullScan = found;
    }

    if (!found)
    {
        return result;
    }

    result.index      = bestIndex;
    result.distanceSq = bestDistance;
    result.found      = true;

    return result;
}
