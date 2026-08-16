#pragma once

#include <Windows.h>

namespace Threading
{
    // A reader/writer lock for data shared between the game thread and the
    // render thread.
    //
    // Those really are two threads. OnFrameUpdate runs on the game thread;
    // OnDraw3D and OnDrawUI run on the render thread, inside the SDK's
    // ZRenderDevice::Present hook. Anything a feature both records and draws is
    // touched by both, and a std::vector that reallocates under a reader hands
    // that reader a freed pointer.
    //
    // SRWLOCK rather than std::mutex on purpose: this project has already been
    // bitten once by the MSVC std::mutex constexpr-constructor regression, which
    // stopped the game launching on an older Visual C++ runtime. The Win32
    // primitive has no such history, needs no runtime support, and is what the
    // diagnostics code here already uses.
    class Lock
    {
    public:
        SRWLOCK handle = SRWLOCK_INIT;
    };

    // Held while reading. Several readers may hold it at once.
    class ReadGuard
    {
    public:
        explicit ReadGuard(const Lock& lock)
            : m_lock(const_cast<SRWLOCK*>(&lock.handle))
        {
            AcquireSRWLockShared(m_lock);
        }

        ~ReadGuard() { ReleaseSRWLockShared(m_lock); }

        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;

    private:
        SRWLOCK* m_lock;
    };

    // Held while writing. Excludes everyone.
    class WriteGuard
    {
    public:
        explicit WriteGuard(Lock& lock) : m_lock(&lock.handle)
        {
            AcquireSRWLockExclusive(m_lock);
        }

        ~WriteGuard() { ReleaseSRWLockExclusive(m_lock); }

        WriteGuard(const WriteGuard&) = delete;
        WriteGuard& operator=(const WriteGuard&) = delete;

    private:
        SRWLOCK* m_lock;
    };
}
