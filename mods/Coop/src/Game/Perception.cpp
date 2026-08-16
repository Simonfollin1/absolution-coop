#include <format>
#include <string>

#include "Game/Perception.h"
#include "Game/ConfigVars.h"
#include "Diag/Diag.h"

namespace Coop::Game
{
    namespace
    {
        // Every switch, with what it becomes while somebody is down and what to
        // fall back on if the variable could not be read first.
        //
        // The visual cone goes to zero degrees, hearing is switched off at its
        // own flag, and the target tracker is given no range and no field of
        // view. Between them there is nothing left to notice anybody with.
        struct Switch
        {
            const char* name;
            const char* off;
            const char* fallback;   // used when the current value is unknown
        };

        constexpr Switch kSwitches[] = {
            { "DetectConeHalfAngle",           "0",  "15"  },
            { "ai_AudioPerceptionDisable",     "1",  "0"   },
            { "TargetTrackerDefaultFarDist",   "0",  "20"  },
            { "TargetTrackerDefaultNearDist",  "0",  "0.01"},
            { "TargetTrackerFovFactorX",       "0",  "0.5" },
            { "TargetTrackerFovFactorY",       "0",  "0.5" },
            { "AI_InstinctVisibilityDistance", "0",  "20"  },
        };
    }

    void Perception::Suppress(ConfigVars& known)
    {
        if (m_suppressed)
        {
            return;
        }

        m_suppressed = true;
        m_saved.clear();

        int applied = 0;
        int guessed = 0;

        for (const Switch& entry : kSwitches)
        {
            Saved saved;

            saved.name       = entry.name;
            saved.suppressed = entry.off;

            // What it holds right now, so the exact value goes back rather than
            // whatever this file thinks the default is. The enumeration has to
            // have run for that - it does, six seconds into every level - and
            // the fallback covers the case where it has not.
            const ConfigVar* current = known.Walked() ? known.Get(entry.name) : nullptr;

            if (current)
            {
                saved.original = current->ValueText();
            }
            else
            {
                saved.original = entry.fallback;
                ++guessed;
            }

            if (ConfigVars::Set(entry.name, entry.off))
            {
                ++applied;
            }

            m_saved.push_back(std::move(saved));
        }

        // What actually happened, read back from the engine rather than assumed
        // from the dispatcher returning without faulting. RefreshValues re-reads
        // what the walk already found, so it costs a pass over a list rather
        // than another walk of the chain.
        int landed = 0;

        if (known.Walked())
        {
            known.RefreshValues();

            for (const Saved& saved : m_saved)
            {
                const ConfigVar* now = known.Get(saved.name);

                const bool took = now && now->ValueText() == saved.suppressed;

                if (took)
                {
                    ++landed;
                }

                Diag::Log("  perception %-32s %s -> %s   %s",
                          saved.name.c_str(), saved.original.c_str(),
                          saved.suppressed.c_str(),
                          !now  ? "NOT IN THE REGISTRY"
                        : took  ? "took"
                                : ("did NOT take, still " + now->ValueText()).c_str());
            }
        }

        m_note = std::format(
            "perception: {} of {} switches dispatched, {} confirmed changed{}",
            applied, static_cast<int>(m_saved.size()), landed,
            guessed > 0 ? " (some originals came from defaults)" : "");

        Diag::Log("%s", m_note.c_str());
    }

    void Perception::Restore()
    {
        if (!m_suppressed)
        {
            return;
        }

        m_suppressed = false;

        for (const Saved& saved : m_saved)
        {
            ConfigVars::Set(saved.name, saved.original);
        }

        m_note = std::format("perception restored: {} switches", m_saved.size());

        m_saved.clear();
    }
}
