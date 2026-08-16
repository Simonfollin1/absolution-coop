#pragma once

// Building the "<name>  12m" labels drawn above NPCs and objects.
//
// One label is nothing. One label per actor per frame, with snprintf parsing a
// format string every time, is the third hot loop in this mod - and it is the
// one where the work is pure formatting, which is exactly what a bench can
// measure honestly.
namespace Hot
{
    // Writes a non-negative distance in whole metres. Returns the length
    // written, not counting the terminator.
    //
    // Rounds half away from zero where snprintf("%.0f") rounds half to even.
    // The two disagree only when the distance is exactly n.5 metres, which is a
    // label on screen for one frame, so the difference is not worth the divide
    // it would take to match.
    int FormatMetres(char* out, int capacity, float metres);

    // "<name>  <n>m", or just the name when withDistance is false. Always
    // terminated, always inside capacity, and truncates the name rather than
    // the distance - the number is the part being read.
    int FormatNameDistance(char* out, int capacity, const char* name,
                           float metres, bool withDistance);
}
