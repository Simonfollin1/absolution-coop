#pragma once

#include <string>
#include <vector>

namespace Coop::Game
{
    class ConfigVars;

    // Turning the guards' senses off while a player is down, and putting them
    // back afterwards.
    //
    // The first attempt at this problem hid the body and moved it out from
    // under the level, which works and is a dodge: the AI still had every sense
    // it started with, it just had nothing to point them at. The engine has the
    // switches themselves, and enumerating all 1648 registered configuration
    // variables in the running game is what turned them up -
    //
    //     DetectConeHalfAngle          float  15     the visual cone, in degrees
    //     ai_AudioPerceptionDisable    int    0      hearing, as an off switch
    //     TargetTrackerDefaultFarDist  float  20     how far targets are tracked
    //     TargetTrackerFovFactorX / Y  float  0.5    and how wide
    //     AI_InstinctVisibilityDistance float 20
    //
    // - none of which needed reverse engineering, because they were sitting in
    // a list the mod already knew how to read and nobody had looked.
    //
    // These are global: while they are off, no NPC perceives anything, not just
    // the downed player. For the seconds somebody is on the floor that is the
    // right trade, and it is exactly as long as it needs to be.
    class Perception
    {
    public:
        // Remembers what each switch held, then turns it off. Safe to call
        // repeatedly; only the first call in a run records the originals.
        void Suppress(const ConfigVars& known);

        // Puts back precisely what was there. Safe when nothing was suppressed.
        void Restore();

        bool Suppressed() const { return m_suppressed; }

        const std::string& Note() const { return m_note; }

    private:
        struct Saved
        {
            std::string name;
            std::string original;
            std::string suppressed;
        };

        std::vector<Saved> m_saved;

        bool        m_suppressed = false;
        std::string m_note;
    };
}
