#include <Windows.h>

#include <cstring>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Player/ZHitman5.h>
#include <Global.h>

#include "Game/Instinct.h"

namespace Coop::Game
{
    namespace
    {
        // ZHM5FocusController as the SDK declares it. Mirrored rather than
        // used directly because every member is private and the only public
        // method is a setter:
        //
        //   bool   m_bIsGaining                    +0x00 (padded to 4)
        //   float  m_fFocusGainTechniqueModifier   +0x04
        //   float  m_fFocusMaxTechniqueModifier    +0x08
        //   float* m_pFocus                        +0x0C
        //
        // Only the pointer is wanted, and it is followed once per frame.
        constexpr size_t kFocusPointerOffset = 0x0C;

        // POD only: __try cannot share a function with anything that unwinds.
        bool ReadFloatVia(const void* controller, size_t offset, float& valueOut)
        {
            __try
            {
                const auto* slot = reinterpret_cast<const uint8_t*>(controller) + offset;

                float* pointer = nullptr;
                memcpy(&pointer, slot, sizeof(pointer));

                if (!pointer)
                {
                    return false;
                }

                valueOut = *pointer;

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
    }

    void Instinct::Update(float deltaSeconds)
    {
        m_readable = false;

        ZHitman5* hitman = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr;

        if (!hitman)
        {
            m_amount    = -1.f;
            m_previous  = -1.f;
            m_activeFor = 0.f;

            return;
        }

        ZHM5FocusController* controller = hitman->GetFocusController();

        float value = 0.f;

        if (!controller || !ReadFloatVia(controller, kFocusPointerOffset, value))
        {
            m_amount   = -1.f;
            m_previous = -1.f;

            return;
        }

        m_readable = true;
        m_amount   = value;

        // Spending it is what holding the key does; it refills on its own, and
        // slowly, so a fall is unambiguous. The threshold is per second so it
        // does not depend on frame rate, and small enough that the slowest
        // drain still reads as one.
        constexpr float kDrainPerSecond = 0.01f;

        // Held briefly after the fall stops, so a frame where the value happens
        // not to change does not flicker the view off and on again.
        constexpr float kHoldSeconds = 0.25f;

        if (m_previous >= 0.f && deltaSeconds > 0.f &&
            (m_previous - m_amount) / deltaSeconds > kDrainPerSecond)
        {
            m_activeFor = kHoldSeconds;
        }
        else
        {
            m_activeFor = m_activeFor > deltaSeconds ? m_activeFor - deltaSeconds : 0.f;
        }

        m_previous = m_amount;
    }
}
