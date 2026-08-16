#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>

#include "Game/HitInspector.h"
#include "Memory/GameOffsets.h"

namespace Coop::Game
{
    namespace
    {
        // POD only, and no objects with destructors, because a function using
        // __try cannot also own something that needs unwinding. The caller does
        // the owning; this does the reading.
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

        // A float that could plausibly be a game quantity rather than a
        // reinterpreted pointer or a bitfield. Damage, health and distances all
        // land in here; 0x00401000 read as a float does not.
        bool LooksLikeAQuantity(float value)
        {
            if (!std::isfinite(value))
            {
                return false;
            }

            if (value == 0.f)
            {
                return true;
            }

            const float magnitude = std::fabs(value);

            return magnitude >= 0.0001f && magnitude <= 100000.f;
        }
    }

    HitInspector& TheHitInspector()
    {
        static HitInspector instance;

        return instance;
    }

    void HitInspector::Capture(const void* hitInfo, uintptr_t callerAddress)
    {
        if (!m_enabled)
        {
            return;
        }

        HitCapture capture;

        capture.address = reinterpret_cast<uintptr_t>(hitInfo);

        const uintptr_t base = GameOffsets::GetModuleBase();

        capture.callerRva = (base && callerAddress > base) ? callerAddress - base : callerAddress;

        // Outside the lock: this is the part that can fault, and holding a lock
        // across a structured exception is how a deadlock gets written.
        if (hitInfo)
        {
            capture.readable = CopyGuarded(hitInfo, capture.bytes, kHitCaptureBytes);
        }

        const Threading::WriteGuard guard(m_lock);

        capture.ordinal = ++m_total;

        // Newest first, so the panel reads top-down in the order things
        // happened to you.
        m_captures.insert(m_captures.begin(), capture);

        if (m_captures.size() > kKeep)
        {
            m_captures.pop_back();
        }
    }

    std::vector<HitCapture> HitInspector::Snapshot() const
    {
        const Threading::ReadGuard guard(m_lock);

        return m_captures;
    }

    size_t HitInspector::Kept() const
    {
        const Threading::ReadGuard guard(m_lock);

        return m_captures.size();
    }

    void HitInspector::Clear()
    {
        const Threading::WriteGuard guard(m_lock);

        m_captures.clear();
        m_total = 0;
    }

    std::vector<uint32_t> HitInspector::ValuesAt(size_t dwordIndex) const
    {
        std::vector<uint32_t> values;

        if (dwordIndex >= kHitCaptureBytes / sizeof(uint32_t))
        {
            return values;
        }

        const Threading::ReadGuard guard(m_lock);

        for (const HitCapture& capture : m_captures)
        {
            if (!capture.readable)
            {
                continue;
            }

            uint32_t value = 0;
            memcpy(&value, capture.bytes + dwordIndex * sizeof(uint32_t), sizeof(value));

            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(value);
            }
        }

        return values;
    }

    bool HitInspector::VariesAt(size_t dwordIndex) const
    {
        return ValuesAt(dwordIndex).size() > 1;
    }

    std::vector<std::pair<uintptr_t, uint32_t>> HitInspector::Callers() const
    {
        std::map<uintptr_t, uint32_t> counts;

        {
            const Threading::ReadGuard guard(m_lock);

            for (const HitCapture& capture : m_captures)
            {
                ++counts[capture.callerRva];
            }
        }

        std::vector<std::pair<uintptr_t, uint32_t>> result(counts.begin(), counts.end());

        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        return result;
    }

    std::string DescribeDword(uint32_t value)
    {
        float asFloat = 0.f;
        memcpy(&asFloat, &value, sizeof(asFloat));

        if (LooksLikeAQuantity(asFloat) && value != 0)
        {
            return std::format("{:08X}  {:>12.4f}  {}", value, asFloat,
                               static_cast<int32_t>(value));
        }

        return std::format("{:08X}  {:>12}  {}", value, "-", static_cast<int32_t>(value));
    }
}
