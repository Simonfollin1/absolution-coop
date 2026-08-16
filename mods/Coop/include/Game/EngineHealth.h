#pragma once

#include <cstdint>
#include <string>

namespace Coop::Game
{
    // The player's real health, read straight out of the game.
    //
    // Found by reading the function that draws the ring around the radar -
    // FUN_004fc060, the only reference to "_root.g_mcHealthBar" - which is 236
    // bytes long and does exactly two interesting things:
    //
    //     local_8 = *(float *)(hud + 0x640);           // health now
    //     if (*(float *)(hud + 0x644) != local_8) {    // health last drawn
    //         ...
    //         local_8 = (local_8 * 100.0f)
    //                 / *(float *)(*(int *)(g_level + 0xa2c) + 0x21c);
    //
    // `hud` is ZHUDManager: the SDK puts it at base + 0xD61C00, and the
    // community autosplitter's "in menu" flag is base + 0xD61C7B, the same
    // object plus 0x7B - which is the byte this function checks at +0x79.
    //
    // So the current health is a plain float at a fixed offset in an object the
    // SDK already hands us a pointer to, and the maximum is two pointers away
    // from a global. No hook, no pattern scan, no address for the value itself.
    //
    // What this unlocks, once confirmed: the mod can stop keeping a shadow
    // health pool. Letting the engine take the damage and mirroring what it
    // did makes close combat, falls, drowning and scripted kills all count -
    // none of which reach the interception - and makes the ring on the HUD move
    // by itself, because the game is still the one moving it.
    class EngineHealth
    {
    public:
        void Update();

        bool  Readable() const { return m_readable; }
        float Current() const { return m_current; }
        float Max() const { return m_max; }

        float Fraction() const;

        // How much it dropped since the previous frame, or zero. This is the
        // number a mirrored health model would subtract from its own pool -
        // and unlike the interception, it sees every kind of damage.
        float LastDrop() const { return m_lastDrop; }

        // Where the values were read from, for the log and the panel.
        const std::string& Note() const { return m_note; }

        // Copies the object the maximum lives in, so the log can show which
        // nearby float is the writable current health. Returns bytes copied.
        size_t SnapshotHealthObject(uint8_t* destination, size_t count) const;

        // The object's address, or zero.
        uintptr_t HealthObject() const { return m_healthObject; }

    private:
        bool      m_readable     = false;
        float     m_current      = -1.f;
        float     m_previous     = -1.f;
        float     m_max          = -1.f;
        float     m_lastDrop     = 0.f;
        uintptr_t m_healthObject = 0;

        std::string m_note;
    };
}
