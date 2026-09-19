#pragma once

#include "dsp/AmpSpeakerLoad.h"

#include <atomic>
#include <chrono>
#include <cstdint>

// The one piece of state shared between pedals: which speaker is on the end
// of the amp. A guitar amp's Presence/Resonance interact with the speaker
// it is actually driving (see dsp/AmpSpeakerLoad.h), and in this rack the
// speaker is the Cabinet pedal - a different block from the amp. So the
// Cabinet PUBLISHES the electrical model of its selected speaker here every
// block, and an amp pedal READS it (the load defines the amp's behavior,
// which is why the data flows against the audio).
//
//  - Lock-free both ways (audio threads never block): a seqlock, so the
//    reader always sees one consistent speaker, never a mix of two.
//  - Not sticky: a reading is only valid if the Cabinet published in the
//    last 250 ms. A bypassed or removed Cabinet stops publishing, and the
//    amp falls back to its own generic speaker instead of living with a
//    stale one.
//  - One Cabinet is assumed. With two, whichever publishes last wins.
//    (Dual-cab mode publishes the LEFT cab; two cabs in parallel would be a
//    different impedance than either.)
namespace SpeakerLoadLink
{
    constexpr auto validFor = std::chrono::milliseconds(250);

    namespace detail
    {
        struct State
        {
            std::atomic<std::uint32_t> sequence { 0 };  // odd while a write is in progress
            std::atomic<double> re { 0.0 }, le { 0.0 }, fs { 0.0 }, qms { 0.0 }, qes { 0.0 };
            std::atomic<std::int64_t> lastPublishNs { 0 };
            std::atomic<bool> everPublished { false };
            std::atomic_flag writing = ATOMIC_FLAG_INIT;
        };

        inline State& state() noexcept
        {
            static State s;
            return s;
        }

        inline std::int64_t nowNs() noexcept
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    // Called by the Cabinet, once per block.
    inline void publish(const AmpSpeakerLoad::Speaker& speaker) noexcept
    {
        auto& s = detail::state();

        // A second publisher (two Cabinets) just skips this round rather
        // than corrupting the sequence.
        if (s.writing.test_and_set(std::memory_order_acquire))
            return;

        // Standard seqlock writer: mark odd, fence, write the data, mark even.
        // The release fence keeps the data stores from being reordered before
        // the "write in progress" marker.
        auto seq = s.sequence.load(std::memory_order_relaxed);
        s.sequence.store(seq + 1, std::memory_order_relaxed); // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);
        s.re.store(speaker.re, std::memory_order_relaxed);
        s.le.store(speaker.le, std::memory_order_relaxed);
        s.fs.store(speaker.fs, std::memory_order_relaxed);
        s.qms.store(speaker.qms, std::memory_order_relaxed);
        s.qes.store(speaker.qes, std::memory_order_relaxed);
        s.sequence.store(seq + 2, std::memory_order_release); // even: done

        s.lastPublishNs.store(detail::nowNs(), std::memory_order_relaxed);
        s.everPublished.store(true, std::memory_order_relaxed);
        s.writing.clear(std::memory_order_release);
    }

    enum class ReadResult
    {
        ok,      // `out` holds the Cabinet's current speaker
        none,    // nobody has published within the last 250 ms: use your own default speaker
        busy     // a Cabinet IS publishing but this read collided with a write: keep what you had
    };

    // Called by an amp, once per block.
    inline ReadResult read(AmpSpeakerLoad::Speaker& out) noexcept
    {
        auto& s = detail::state();
        if (! s.everPublished.load(std::memory_order_relaxed))
            return ReadResult::none;
        if (detail::nowNs() - s.lastPublishNs.load(std::memory_order_relaxed)
              > std::chrono::duration_cast<std::chrono::nanoseconds>(validFor).count())
            return ReadResult::none;

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            // Standard seqlock reader: read the sequence, read the data, fence,
            // re-read the sequence. The acquire fence keeps the data loads
            // from being reordered after the second sequence read.
            auto before = s.sequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue; // a write is in progress

            AmpSpeakerLoad::Speaker read;
            read.re = s.re.load(std::memory_order_relaxed);
            read.le = s.le.load(std::memory_order_relaxed);
            read.fs = s.fs.load(std::memory_order_relaxed);
            read.qms = s.qms.load(std::memory_order_relaxed);
            read.qes = s.qes.load(std::memory_order_relaxed);

            std::atomic_thread_fence(std::memory_order_acquire);
            if (s.sequence.load(std::memory_order_relaxed) == before)
            {
                out = read;
                return ReadResult::ok;
            }
        }
        return ReadResult::busy;
    }

    // Called when a Cabinet goes away: forget the speaker immediately rather
    // than waiting out the timeout.
    inline void clear() noexcept
    {
        detail::state().everPublished.store(false, std::memory_order_relaxed);
    }
}
