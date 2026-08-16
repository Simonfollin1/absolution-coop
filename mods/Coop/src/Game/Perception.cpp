#include <format>

#include "Game/Perception.h"
#include "Game/ConfigVars.h"

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

    void Perception::Suppress(const ConfigVars& known)
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

        m_note = guessed == 0
            ? std::format("perception off: {} switches, originals recorded", applied)
            : std::format("perception off: {} switches, {} restored from defaults "
                          "because the variables had not been read yet", applied, guessed);
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
