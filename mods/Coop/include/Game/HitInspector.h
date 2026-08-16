#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Threading/Lock.h"

namespace Coop::Game
{
    // What every intercepted hit actually contained.
    //
    // The mod charges a flat cost per hit because the real damage figure lives
    // behind SHitInfo::GetBaseDamage, which is a call with no retail address.
    // But SHitInfo itself arrives by reference on every hit, and its contents
    // are right there - so rather than reverse the accessor, capture the struct
    // and look at it.
    //
    // Two things come out of a session with this on:
    //
    //   1. The damage figure, if it is a plain field. A slot that reads 20 for
    //      a pistol and 60 for a shotgun is the answer, and the panel marks
    //      which slots vary across captures so it is one glance rather than a
    //      spreadsheet.
    //   2. The address of whoever called YouGotHit. That is the code that built
    //      the SHitInfo, so it is where the damage was computed, and captured
    //      as an RVA it can be opened directly in a disassembler.
    //
    // Neither is obtainable outside a running game, which is the whole reason
    // this exists.

    // Comfortably past everything the 2012 development build's headers put in
    // SHitInfo, without being so much that a short one runs off its page.
    constexpr size_t kHitCaptureBytes = 0x60;

    struct HitCapture
    {
        uint32_t ordinal = 0;

        // Where the call came from, as an offset into HMA.exe. Paste it into
        // Ghidra as base + this.
        uintptr_t callerRva = 0;

        // The struct's own address, and how much of it came back. A hit whose
        // reference could not be read is worth seeing rather than hiding, and
        // a short read is worth distinguishing from a failed one: SHitInfo may
        // simply end near the top of a page.
        uintptr_t address   = 0;
        bool      readable  = false;
        uint32_t  byteCount = 0;

        uint8_t bytes[kHitCaptureBytes]{};
    };

    // Written on the game thread from inside the vtable thunk, read on the
    // render thread by the panel. Both really do happen at once, so every
    // accessor takes the lock and hands back a copy rather than a reference
    // into a vector that can reallocate mid-read.
    class HitInspector
    {
    public:
        // Called from the vtable thunk, inside somebody else's call. Copies and
        // returns; the lock is held for a memcpy and nothing more.
        void Capture(const void* hitInfo, uintptr_t callerAddress);

        // A copy, deliberately. Two dozen small structs is nothing next to
        // handing the render thread a pointer the game thread can free.
        std::vector<HitCapture> Snapshot() const;

        size_t   Kept() const;
        uint32_t Total() const { return m_total; }
        bool     Enabled() const { return m_enabled; }
        void     SetEnabled(bool enabled) { m_enabled = enabled; }

        void Clear();

        // True when this dword offset held more than one distinct value across
        // the captures kept. A damage field varies with the weapon; a padding
        // field does not.
        bool VariesAt(size_t dwordIndex) const;

        // The distinct values seen at one dword offset, most recent first.
        std::vector<uint32_t> ValuesAt(size_t dwordIndex) const;

        // Every caller seen, with how many hits came from each. Two entries
        // means two different damage paths - melee and gunfire, say.
        std::vector<std::pair<uintptr_t, uint32_t>> Callers() const;

    private:
        // Enough to see a pistol and a shotgun in the same window without the
        // buffer being large enough to matter.
        static constexpr size_t kKeep = 24;

        mutable Threading::Lock m_lock;

        std::vector<HitCapture> m_captures;

        // Written under the lock, read without it. A count that is one frame
        // stale in a panel is not worth making the hit path contend for.
        uint32_t m_total   = 0;
        bool     m_enabled = true;
    };

    HitInspector& TheHitInspector();

    // A dword rendered three ways, because which one is meaningful is exactly
    // what we do not know yet.
    std::string DescribeDword(uint32_t value);
}
