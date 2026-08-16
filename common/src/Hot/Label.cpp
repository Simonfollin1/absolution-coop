#include "Hot/Label.h"

namespace
{
    // Distances in this game are metres inside a level, so five digits is more
    // than the map is wide. Anything beyond it is a bad read rather than a real
    // position, and clamping keeps the buffer arithmetic below provable.
    constexpr int MAX_METRES = 99999;

    int WriteUnsigned(char* out, int capacity, int value)
    {
        char digits[8];
        int  count = 0;

        do
        {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (value > 0 && count < static_cast<int>(sizeof(digits)));

        if (count > capacity)
        {
            return 0;
        }

        for (int i = 0; i < count; ++i)
        {
            out[i] = digits[count - 1 - i];
        }

        return count;
    }
}

int Hot::FormatMetres(char* out, int capacity, float metres)
{
    if (!out || capacity <= 1)
    {
        if (out && capacity > 0)
        {
            out[0] = '\0';
        }

        return 0;
    }

    int whole = 0;

    if (metres > 0.f)
    {
        // Half away from zero, which is what a reader expects a rounded metre
        // to mean, and one add rather than a call into the CRT.
        const float rounded = metres + 0.5f;

        whole = rounded >= static_cast<float>(MAX_METRES)
            ? MAX_METRES
            : static_cast<int>(rounded);
    }

    const int written = WriteUnsigned(out, capacity - 1, whole);

    out[written] = '\0';

    return written;
}

int Hot::FormatNameDistance(char* out, int capacity, const char* name,
                            float metres, bool withDistance)
{
    if (!out || capacity <= 0)
    {
        return 0;
    }

    if (capacity == 1)
    {
        out[0] = '\0';

        return 0;
    }

    // The distance is the part being read, so its room is reserved before the
    // name is copied rather than after: two spaces, up to five digits, and the
    // m. A long actor name then truncates instead of pushing the number out.
    constexpr int DISTANCE_ROOM = 2 + 5 + 1;

    const int limit = withDistance
        ? (capacity - 1 > DISTANCE_ROOM ? capacity - 1 - DISTANCE_ROOM : 0)
        : capacity - 1;

    int length = 0;

    if (name)
    {
        while (name[length] && length < limit)
        {
            out[length] = name[length];
            ++length;
        }
    }

    if (length == 0 && limit > 0)
    {
        out[length++] = '?';
    }

    if (withDistance && length + DISTANCE_ROOM < capacity)
    {
        out[length++] = ' ';
        out[length++] = ' ';

        length += Hot::FormatMetres(out + length, capacity - length, metres);

        if (length + 1 < capacity)
        {
            out[length++] = 'm';
        }
    }

    out[length] = '\0';

    return length;
}
