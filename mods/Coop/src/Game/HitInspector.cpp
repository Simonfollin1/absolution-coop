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
        //
        // Backing off rather than giving up, because a struct that ends near
        // the top of a page would otherwise come back as nothing at all - and
        // the first sixteen bytes are worth more than a failure.
        if (hitInfo)
        {
            for (size_t want = kHitCaptureBytes; want >= 0x10; want /= 2)
            {
                if (CopyGuarded(hitInfo, capture.bytes, want))
                {
                    capture.readable  = true;
                    capture.byteCount = static_cast<uint32_t>(want);

                    break;
                }
            }
        }

        // One level further, to the projectile. This is the object
        // GetBaseDamage reads, so if a damage number exists anywhere reachable
        // from a hit, it is in here rather than in the hit itself.
        if (capture.readable && capture.byteCount > kProjectileOffsetInHit + sizeof(void*))
        {
            uintptr_t projectile = 0;
            memcpy(&projectile, capture.bytes + kProjectileOffsetInHit, sizeof(projectile));

            // The game is 32-bit, so anything outside user space is not a
            // pointer and following it would only find an access violation.
            if (projectile >= 0x10000 && projectile < 0x7FFF0000)
            {
                capture.projectile = projectile;

                for (size_t want = kProjectileCaptureBytes; want >= 0x10; want /= 2)
                {
                    if (CopyGuarded(reinterpret_cast<const void*>(projectile),
                                    capture.projectileBytes, want))
                    {
                        capture.projectileByteCount = static_cast<uint32_t>(want);
                        break;
                    }
                }
            }
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

        const size_t offset = dwordIndex * sizeof(uint32_t);

        for (const HitCapture& capture : m_captures)
        {
            // A short capture has nothing to say about an offset it never
            // reached, and counting its zeroes as a value would make every
            // slot past the truncation look like it varies.
            if (!capture.readable || offset + sizeof(uint32_t) > capture.byteCount)
            {
                continue;
            }

            uint32_t value = 0;
            memcpy(&value, capture.bytes + offset, sizeof(value));

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
