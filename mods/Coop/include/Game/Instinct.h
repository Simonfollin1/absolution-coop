#pragma once

#include <cstdint>

namespace Coop::Game
{
    // Whether the player is holding Instinct, and how much of it is left.
    //
    // Instinct is "focus" inside the engine, and ZHM5FocusController holds a
    // float* to the live value. The SDK declares the whole class layout and
    // keeps the members private with no accessor, so this reads the field at
    // its declared offset - which is not a guess: the declaration order is in
    // the header, and every other member of it is used by the SDK's own mods.
    //
    // Why it matters for co-op: Instinct is the game's own see-through-walls
    // view, with points of interest drawn in it. Teammates belong in that view
    // and nowhere else - a marker hanging over a wall at all times is a cheat,
    // and the same marker inside Instinct is the feature the game already has.
    class Instinct
    {
    public:
        // Reads the controller once. Cheap enough for every frame.
        void Update(float deltaSeconds);

        // True while the meter is being spent, which is what holding the key
        // does. Detected from the value falling rather than from the key,
        // because the key can be rebound and the meter cannot.
        bool Active() const { return m_activeFor > 0.f; }

        // 0 to 1, or -1 when the controller could not be read.
        float Amount() const { return m_amount; }

        bool Readable() const { return m_readable; }

    private:
        float m_amount    = -1.f;
        float m_previous  = -1.f;
        float m_activeFor = 0.f;
        bool  m_readable  = false;
    };
}
