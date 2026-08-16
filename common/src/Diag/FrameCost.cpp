#include "Diag/FrameCost.h"
#include "Diag/Diag.h"

#include <Windows.h>

namespace
{
    // Fixed for the life of the process, so it is worth asking for once.
    long long TicksPerSecond()
    {
        static long long frequency = []
        {
            LARGE_INTEGER value = {};
            QueryPerformanceFrequency(&value);

            return value.QuadPart > 0 ? value.QuadPart : 1;
        }();

        return frequency;
    }

    long long Now()
    {
        LARGE_INTEGER value = {};
        QueryPerformanceCounter(&value);

        return value.QuadPart;
    }
}

void Diag::FrameCost::Configure(const char* label, int reportEveryFrames)
{
    m_label       = label ? label : "frame";
    m_reportEvery = reportEveryFrames > 0 ? reportEveryFrames : 3600;
}

void Diag::FrameCost::Begin()
{
    m_start = Now();
}

void Diag::FrameCost::End()
{
    const long long elapsed = Now() - m_start;

    // A negative reading means the counter was read across a correction. Drop
    // it rather than letting it poison an average.
    if (elapsed < 0)
    {
        return;
    }

    m_total += elapsed;
    ++m_frames;
    ++m_totalFrames;

    if (elapsed > m_worst)
    {
        m_worst = elapsed;
    }

    // The live figures, updated every sample so a panel has something to show
    // between report lines.
    const double tickRate = static_cast<double>(TicksPerSecond());
    const float  sampleUs = static_cast<float>(static_cast<double>(elapsed) / tickRate * 1e6);

    m_lastUs = sampleUs;

    // Same smoothing the overlay uses for its frame rate, and for the same
    // reason: an unsmoothed per-frame figure flickers too fast to read off a
    // screen, and reading it off the screen is the entire point.
    m_averageUs = m_averageUs > 0.f ? (m_averageUs * 0.95f + sampleUs * 0.05f) : sampleUs;

    if (sampleUs > m_windowWorstUs)
    {
        m_windowWorstUs = sampleUs;
    }

    if (m_frames < m_reportEvery)
    {
        return;
    }

    const double ticks       = static_cast<double>(TicksPerSecond());
    const double averageUs   = (static_cast<double>(m_total) / m_frames) / ticks * 1e6;
    const double worstUs     = static_cast<double>(m_worst) / ticks * 1e6;

    // Against a 60fps budget, because "microseconds" means little on its own
    // and "percent of a frame" is the question being asked.
    const double shareOfFrame = averageUs / 16666.0 * 100.0;

    Diag::Log("%s cost over %d frames: %.1f us average, %.1f us worst (%.3f%% of a 60fps frame)",
        m_label, m_frames, averageUs, worstUs, shareOfFrame);

    m_total  = 0;
    m_worst  = 0;
    m_frames = 0;

    // The displayed worst follows the reporting window rather than running for
    // the whole session, so one hitch during a level load does not sit in the
    // panel for an hour hiding everything that happened since.
    m_windowWorstUs = 0.f;
}
