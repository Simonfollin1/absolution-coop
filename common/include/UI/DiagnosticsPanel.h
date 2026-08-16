#pragma once

#include "Diag/FrameCost.h"

namespace UI
{
    // The numbers a screenshot has to carry for somebody who is not at the
    // machine to say anything useful about performance.
    //
    // Every mod builds its own copy of common/, so this is per-mod by
    // construction - there is no cross-mod view to be had from inside one DLL,
    // and pretending otherwise would be the reason the numbers disagreed. Two
    // screenshots, one per mod, is the honest shape.
    //
    // What it deliberately shows:
    //
    //   - Each per-frame path separately. "The mod costs X" cannot be acted on;
    //     "the world lines cost X and the panels cost nothing" can.
    //   - A percentage against a 60fps frame, because microseconds mean nothing
    //     without the budget they are spent from.
    //   - The two threads' own frame counts side by side. The game thread and
    //     the render thread are not synchronised, so whether they tick at the
    //     same rate is a real open question - and one that reading the code
    //     cannot answer but two counters can.
    class DiagnosticsPanel
    {
    public:
        // modName appears in the heading so two screenshots cannot be confused.
        void Configure(const char* modName);

        // Draws the shared block: build, the three paths, thread rates. A mod
        // adds its own rows after calling this.
        void Render(const Diag::FrameCost& update,
                    const Diag::FrameCost& drawUi,
                    const Diag::FrameCost& draw3d);

        // One row of the cost table, for a mod that measures something extra.
        static void CostRow(const Diag::FrameCost& cost);

        // A labelled figure with the same colouring rules as the table, for
        // counts a mod wants to show next to the costs.
        static void Stat(const char* label, int value, const char* note = nullptr);
        static void Stat(const char* label, const char* value, const char* note = nullptr);

    private:
        const char* m_modName = "Mod";

        // Wall-clock at the last reset, so the thread rates can be expressed as
        // frames per second rather than as two totals nobody can divide in
        // their head.
        long long   m_since      = 0;
        unsigned int m_baseUpdate = 0;
        unsigned int m_baseDrawUi = 0;
        unsigned int m_baseDraw3d = 0;

        // Same window, applied to the kernel calls the offset readers make. A
        // total since load says nothing once the game has been open an hour;
        // a rate says whether they are still happening.
        unsigned int m_baseProbes = 0;
        unsigned int m_baseHits   = 0;

        void ResetRates(const Diag::FrameCost& update,
                        const Diag::FrameCost& drawUi,
                        const Diag::FrameCost& draw3d);
    };
}
