#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <format>

#include <Glacier/UI/ZHUDManager.h>
#include <Global.h>

#include "Game/EngineHealth.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    namespace
    {
        // Straight out of FUN_004fc060.
        constexpr uintptr_t kHudCurrentHealth = 0x640;   // float, the live value
        constexpr uintptr_t kHudDrawnHealth   = 0x644;   // float, what the ring shows

        // And the maximum: a global, one pointer, one offset.
        //
        // The global sits 0x48 past ZLevelManager, which the SDK resolves as
        // base + 0xE21310. Whether it is a member of the level manager or a
        // neighbour of it does not matter here - it is a fixed place holding a
        // pointer, and every read below is guarded.
        constexpr uintptr_t kMaxHealthGlobal = 0xE21358;
        constexpr uintptr_t kMaxHealthOwner  = 0xA2C;
        constexpr uintptr_t kMaxHealthField  = 0x21C;

        // A plausible amount of health rather than a reinterpreted pointer.
        bool LooksLikeHealth(float value)
        {
            return value >= 0.f && value <= 100000.f;
        }

        // POD only: __try cannot share a function with anything that unwinds.
        bool ReadFloatGuarded(uintptr_t address, float& valueOut)
        {
            __try
            {
                valueOut = *reinterpret_cast<const float*>(address);

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadPointerGuarded(uintptr_t address, uintptr_t& valueOut)
        {
            __try
            {
                valueOut = *reinterpret_cast<const uintptr_t*>(address);

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool WriteFloatGuarded(uintptr_t address, float value)
        {
            __try
            {
                *reinterpret_cast<float*>(address) = value;

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool CopyGuarded(const void* source, uint8_t* destination, size_t count)
        {
            __try
            {
                memcpy(destination, source, count);

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool LooksLikePointer(uintptr_t value)
        {
            return value >= 0x10000 && value < 0x7FFF0000;
        }
    }

    float EngineHealth::Fraction() const
    {
        if (!m_readable || m_max <= 0.f)
        {
            return -1.f;
        }

        return std::clamp(m_current / m_max, 0.f, 1.f);
    }

    void EngineHealth::Update()
    {
        m_readable = false;
        m_lastDrop = 0.f;

        const uintptr_t base = GameOffsets::GetModuleBase();

        if (!base || !HUDManager)
        {
            m_note = "no HUD manager yet";
            return;
        }

        float current = 0.f;

        if (!ReadFloatGuarded(reinterpret_cast<uintptr_t>(HUDManager) + kHudCurrentHealth,
                              current))
        {
            m_note = "could not read the HUD's health field";
            return;
        }

        if (!LooksLikeHealth(current))
        {
            m_note = std::format("HUD health reads {:.3f}, which is not health", current);
            return;
        }

        // The maximum, two hops from a global. Its absence is not fatal - the
        // current value is the useful one - so a failure here only costs the
        // fraction.
        uintptr_t owner = 0;
        float     max   = -1.f;

        m_healthObject = 0;

        if (ReadPointerGuarded(base + kMaxHealthGlobal, owner) && LooksLikePointer(owner))
        {
            uintptr_t healthObject = 0;

            if (ReadPointerGuarded(owner + kMaxHealthOwner, healthObject) &&
                LooksLikePointer(healthObject))
            {
                m_healthObject = healthObject;

                float candidate = 0.f;

                if (ReadFloatGuarded(healthObject + kMaxHealthField, candidate) &&
                    LooksLikeHealth(candidate) && candidate > 0.f)
                {
                    max = candidate;
                }
            }
        }

        m_readable = true;
        m_max      = max;

        if (m_previous >= 0.f && current < m_previous)
        {
            m_lastDrop = m_previous - current;
        }

        m_previous = current;
        m_current  = current;

        m_note = max > 0.f
            ? std::format("{:.1f} / {:.1f} from the HUD, maximum via {:08X}",
                          current, max, m_healthObject)
            : std::format("{:.1f} from the HUD, maximum unreadable", current);
    }

    float EngineHealth::ReadRaw() const
    {
        if (!HUDManager)
        {
            return -1.f;
        }

        float value = -1.f;

        ReadFloatGuarded(reinterpret_cast<uintptr_t>(HUDManager) + kHudCurrentHealth, value);

        return value;
    }

    bool EngineHealth::Write(float value)
    {
        if (!HUDManager)
        {
            return false;
        }

        const uintptr_t hud = reinterpret_cast<uintptr_t>(HUDManager);

        if (!WriteFloatGuarded(hud + kHudCurrentHealth, value))
        {
            m_note = "the HUD's health field would not take a write";
            return false;
        }

        // The drawing function compares the two and returns early when they
        // match, so writing only the live value can leave the ring showing
        // whatever it drew last. Making the drawn copy disagree is what
        // guarantees the next call redraws.
        WriteFloatGuarded(hud + kHudDrawnHealth, value - 1.f);

        m_lastWritten = value;
        m_previous    = value;
        m_current     = value;

        return true;
    }

    size_t EngineHealth::SnapshotHealthObject(uint8_t* destination, size_t count) const
    {
        if (!m_healthObject || !destination || count == 0)
        {
            return 0;
        }

        return CopyGuarded(reinterpret_cast<const void*>(m_healthObject), destination, count)
            ? count : 0;
    }
}
