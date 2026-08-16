#pragma once

namespace Diag
{
    // Measures what a mod costs per frame and writes it to that mod's log.
    //
    // Exists so "this does not affect performance" can be a number rather than
    // an opinion. Nobody can profile a mod from the outside - the game's own
    // frame time moves for a dozen reasons - but a mod can time its own work
    // precisely, and that is the part anyone is actually asking about.
    //
    // The measurement costs two QueryPerformanceCounter calls, about forty
    // nanoseconds, which is small enough to leave in permanently. A number you
    // can check at any time is worth more than one you have to build a special
    // version to get.
    class FrameCost
    {
    public:
        // label names the work in the log. reportEveryFrames is how often a
        // summary line is written - a minute at 60fps by default, which is
        // often enough to watch and rare enough to ignore.
        void Configure(const char* label, int reportEveryFrames = 3600);

        void Begin();

        // Ends the sample, and writes a summary when the interval is up.
        void End();

        // Live readouts, for a panel that shows the number rather than waiting
        // for the log line.
        //
        // Deliberately floats and a 32-bit count rather than the tick totals
        // below. The game is a 32-bit process and these are read from the
        // render thread while the game thread writes them: a 4-byte aligned
        // load cannot tear, where the 64-bit tick counters could be read
        // half-updated and show a wild figure for one frame.
        //
        // No lock, because a display that is one frame stale is right and a
        // display that costs a lock acquisition on the measured path is not.
        const char*  Label()        const { return m_label; }
        float        LastMicros()   const { return m_lastUs; }
        float        AverageMicros()const { return m_averageUs; }
        float        WorstMicros()  const { return m_windowWorstUs; }
        unsigned int TotalFrames()  const { return m_totalFrames; }

    private:
        const char*   m_label = "frame";
        int           m_reportEvery = 3600;

        long long     m_start = 0;
        long long     m_total = 0;
        long long     m_worst = 0;
        int           m_frames = 0;

        // Smoothed rather than the window mean, which would sit at zero for the
        // first frames after every report and jump when it refilled.
        float         m_lastUs        = 0.f;
        float         m_averageUs     = 0.f;
        float         m_windowWorstUs = 0.f;
        unsigned int  m_totalFrames   = 0;
    };

    // Times a block and closes it however the block exits.
    class FrameCostScope
    {
    public:
        explicit FrameCostScope(FrameCost& cost) : m_cost(&cost) { m_cost->Begin(); }
        ~FrameCostScope() { m_cost->End(); }

        FrameCostScope(const FrameCostScope&) = delete;
        FrameCostScope& operator=(const FrameCostScope&) = delete;

    private:
        FrameCost* m_cost;
    };
}
