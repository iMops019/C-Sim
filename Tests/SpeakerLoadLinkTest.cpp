// Verify the cab -> amp speaker link and what it feeds:
//  - each Cabinet cab type has a sensible electrical speaker (resonance
//    frequency in the range the sources give, small cones resonate higher),
//    and mixed speakers blend on a log frequency scale;
//  - the load filter can swap speakers while running: after the swap it is
//    STILL exactly the bilinear transform of the closed loop for the new
//    speaker, STILL exactly flat at neutral; and the speaker choice changes
//    the amp's Resonance/Presence response measurably but only subtly (a
//    simple "boost lands on the speaker's own peak" rule is NOT what the
//    model says - the loop gain's phase sees to that);
//  - the link itself: nothing before a publish, the published speaker after,
//    nothing again once the Cabinet has been silent for 250 ms or is
//    cleared, and - the point of a seqlock - a reader hammering it while a
//    writer hammers it back never sees a torn (half-old, half-new) speaker.

#include "../Source/SpeakerLoadLink.h"
#include "../Source/dsp/AmpSpeakerLoad.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    double measuredGainDb(AmpSpeakerLoad::Filter& filter, double freq)
    {
        auto settle = static_cast<int>(1.0 * sampleRate);
        auto measure = static_cast<int>(2.0 * sampleRate);
        double inSq = 0.0, outSq = 0.0;
        for (int n = 0; n < settle + measure; ++n)
        {
            auto x = static_cast<float>(0.1 * std::sin(2.0 * pi * freq * n / sampleRate));
            auto y = filter.processSample(x);
            if (n >= settle)
            {
                inSq += static_cast<double>(x) * x;
                outSq += static_cast<double>(y) * y;
            }
        }
        return 10.0 * std::log10(outSq / inSq);
    }

    // The bilinear-transform prediction of the closed loop over its neutral, for a given speaker.
    double predictedDb(const AmpSpeakerLoad::Speaker& sp, double presence, double resonance, double freq)
    {
        AmpSpeakerLoad::Amp amp;
        auto warped = 2.0 * sampleRate * std::tan(pi * freq / sampleRate);
        auto h = std::abs(AmpSpeakerLoad::analogResponse(sp, amp, presence, resonance, warped));
        auto neutral = std::abs(AmpSpeakerLoad::analogResponse(sp, amp, 0.5, 0.5, warped));
        return 20.0 * std::log10(h / neutral);
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // The link is process-wide state: the "nothing published yet" checks
    // must come before anything publishes.
    std::printf("=== The link: nothing until a Cabinet publishes ===\n");
    {
        AmpSpeakerLoad::Speaker out;
        check(SpeakerLoadLink::read(out) == SpeakerLoadLink::ReadResult::none, "before any publish, an amp is told there is no Cabinet");
    }

    std::printf("\n=== Each cab type's speaker ===\n");
    {
        std::printf("  resonance by cab: ");
        double lowest = 1.0e9, highest = 0.0;
        int lowestCab = 0, highestCab = 0;
        bool sensible = true;
        for (int cab = 0; cab < 8; ++cab)
        {
            auto s = AmpSpeakerLoad::speakerForCab(cab);
            std::printf("%d:%.0fHz ", cab, s.fs);
            sensible &= s.fs >= 70.0 && s.fs <= 125.0 && s.re > 4.0 && s.re < 9.0 && s.le > 0.0 && s.qms > 0.0 && s.qes > 0.0;
            if (s.fs < lowest) { lowest = s.fs; lowestCab = cab; }
            if (s.fs > highest) { highest = s.fs; highestCab = cab; }
        }
        std::printf("\n");
        check(sensible, "every cab has a plausible speaker (70-125 Hz resonance, 4-9 ohm voice coil)");
        check(lowestCab == 2, "the open-back 2x12 resonates lowest (no enclosure to raise it)");
        check(highestCab == 4, "the small Fender 1x10 resonates highest");

        auto v30 = AmpSpeakerLoad::speakerForCab(0), fender = AmpSpeakerLoad::speakerForCab(4);
        check(AmpSpeakerLoad::sameSpeaker(AmpSpeakerLoad::mixSpeakers(v30, fender, 0.0), v30), "mix 0 is exactly the first speaker");
        check(AmpSpeakerLoad::sameSpeaker(AmpSpeakerLoad::mixSpeakers(v30, fender, 1.0), fender), "mix 1 is exactly the second");
        auto half = AmpSpeakerLoad::mixSpeakers(v30, fender, 0.5);
        check(std::abs(half.fs - std::sqrt(v30.fs * fender.fs)) < 1.0e-9, "an even mix has the GEOMETRIC mean resonance (frequencies blend by ratio)");
    }

    std::printf("\n=== The load filter swaps speakers and stays exact ===\n");
    {
        auto open = AmpSpeakerLoad::speakerForCab(2), small = AmpSpeakerLoad::speakerForCab(4);

        // Start on one speaker, swap, and measure the swapped filter.
        AmpSpeakerLoad::Filter filter(open);
        filter.prepare(sampleRate);
        filter.setControls(0.2f, 1.0f);
        for (int n = 0; n < 4800; ++n)
            filter.processSample(static_cast<float>(std::sin(2.0 * pi * 300.0 * n / sampleRate)));
        filter.setSpeaker(small);
        check(AmpSpeakerLoad::sameSpeaker(filter.getSpeaker(), small), "setSpeaker takes effect");

        double worst = 0.0;
        for (double f : { 60.0, 115.0, 250.0, 1000.0, 4000.0 })
        {
            AmpSpeakerLoad::Filter probe(open);
            probe.prepare(sampleRate);
            probe.setControls(0.2f, 1.0f);
            probe.setSpeaker(small);
            worst = std::max(worst, std::abs(measuredGainDb(probe, f) - predictedDb(small, 0.2, 1.0, f)));
        }
        std::printf("  after swapping open-back -> Fender 1x10: worst deviation from the new speaker's bilinear prediction %.4f dB\n", worst);
        check(worst < 0.02, "after the swap the filter is exactly the closed loop for the NEW speaker");

        double worstNeutral = 0.0;
        for (double f = 40.0; f <= 10000.0; f *= 1.4)
        {
            AmpSpeakerLoad::Filter probe(open);
            probe.prepare(sampleRate);
            probe.setSpeaker(small);
            worstNeutral = std::max(worstNeutral, std::abs(measuredGainDb(probe, f)));
        }
        std::printf("  neutral (both knobs 0.5) on the swapped speaker: largest deviation from 0 dB %.5f dB\n", worstNeutral);
        check(worstNeutral < 0.01, "and it is still EXACTLY flat at neutral, whatever the speaker");

        // How much does the speaker choice change what Resonance/Presence do?
        // Not by a simple rule: the loop gain has a PHASE, and near a speaker's
        // resonance its impedance phase swings, so "biggest boost at its own
        // peak" is not what the model says (the small speaker actually gets
        // slightly MORE Resonance boost around 75-115 Hz). What is guaranteed is
        // that the choice matters, and by how much: measured, a subtle colour.
        auto v30 = AmpSpeakerLoad::speakerForCab(0);
        double worstResonance = 0.0, worstPresence = 0.0;
        std::printf("  Resonance 100%% boost (dB), open-back 75 Hz / V30 90 Hz / Fender 1x10 115 Hz:\n");
        for (double f : { 40.0, 60.0, 75.0, 90.0, 115.0, 150.0, 300.0 })
        {
            auto a = predictedDb(open, 0.5, 1.0, f), b = predictedDb(v30, 0.5, 1.0, f), c = predictedDb(small, 0.5, 1.0, f);
            std::printf("    %4.0f Hz: %+5.2f / %+5.2f / %+5.2f\n", f, a, b, c);
            worstResonance = std::max(worstResonance, std::max({ a, b, c }) - std::min({ a, b, c }));
        }
        for (double f : { 1000.0, 2000.0, 4000.0, 8000.0 })
            worstPresence = std::max(worstPresence, std::abs(predictedDb(open, 1.0, 0.5, f) - predictedDb(small, 1.0, 0.5, f)));
        std::printf("  largest difference between speakers: Resonance %.2f dB, Presence %.2f dB\n", worstResonance, worstPresence);
        check(worstResonance > 0.3, "the choice of speaker measurably changes the amp's Resonance response (more than 0.3 dB)");
        check(worstResonance < 2.0 && worstPresence < 2.0, "but only as a subtle colour (under 2 dB) - a real amp's feedback masks most of the load");

        // Swapping while playing stays finite and bounded.
        AmpSpeakerLoad::Filter moving(open);
        moving.prepare(sampleRate);
        moving.setControls(0.8f, 0.9f);
        bool finite = true;
        double peak = 0.0;
        for (int n = 0; n < 96000; ++n)
        {
            if (n % 1000 == 0)
                moving.setSpeaker(AmpSpeakerLoad::speakerForCab((n / 1000) % 8));
            auto y = moving.processSample(static_cast<float>(std::sin(2.0 * pi * 110.0 * n / sampleRate)));
            finite &= std::isfinite(y);
            peak = std::max(peak, std::abs(static_cast<double>(y)));
        }
        std::printf("  peak output while switching cab every 1000 samples under a 110 Hz note: %.2f\n", peak);
        check(finite && peak < 8.0, "switching speakers while playing stays finite and bounded");
    }

    std::printf("\n=== The link: publish, read, expire, clear ===\n");
    {
        auto speaker = AmpSpeakerLoad::speakerForCab(4);
        SpeakerLoadLink::publish(speaker);

        AmpSpeakerLoad::Speaker out;
        auto result = SpeakerLoadLink::read(out);
        check(result == SpeakerLoadLink::ReadResult::ok && AmpSpeakerLoad::sameSpeaker(out, speaker),
              "a published speaker is read back exactly");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(SpeakerLoadLink::read(out) == SpeakerLoadLink::ReadResult::none,
              "after 300 ms of silence the reading has expired (a bypassed Cabinet stops defining the load)");

        SpeakerLoadLink::publish(speaker);
        check(SpeakerLoadLink::read(out) == SpeakerLoadLink::ReadResult::ok, "publishing again revives it");
        SpeakerLoadLink::clear();
        check(SpeakerLoadLink::read(out) == SpeakerLoadLink::ReadResult::none, "clear() (a Cabinet being removed) forgets it immediately");
    }

    std::printf("\n=== The link is lock-free and never torn ===\n");
    {
        // The writer publishes speakers whose five fields are all equal to one
        // number k. A reader that ever sees them unequal has seen a torn
        // (half-old, half-new) speaker.
        std::atomic<bool> done { false };
        std::thread writer([&]
        {
            for (int k = 1; k <= 400000; ++k)
            {
                AmpSpeakerLoad::Speaker s;
                s.re = s.le = s.fs = s.qms = s.qes = static_cast<double>(k);
                SpeakerLoadLink::publish(s);
            }
            done.store(true);
        });

        long long reads = 0, torn = 0, busy = 0;
        while (! done.load())
        {
            AmpSpeakerLoad::Speaker s;
            auto r = SpeakerLoadLink::read(s);
            if (r == SpeakerLoadLink::ReadResult::ok)
            {
                ++reads;
                if (! (s.re == s.le && s.le == s.fs && s.fs == s.qms && s.qms == s.qes))
                    ++torn;
            }
            else if (r == SpeakerLoadLink::ReadResult::busy)
                ++busy;
        }
        writer.join();
        std::printf("  %lld clean reads against a writer publishing 400000 times: %lld torn, %lld collisions reported as busy\n",
                     reads, torn, busy);
        check(reads > 1000, "the reader really did read while the writer was running");
        check(torn == 0, "not one torn read");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
