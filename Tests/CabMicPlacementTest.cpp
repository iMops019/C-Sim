// End-to-end check that the Cabinet pedal actually APPLIES mic placement
// (dsp/MicPlacement.h is tested against the physics on its own; this
// checks the wiring, through the real CabinetPedal and real convolution):
//
//  1. equal distances never delay anything, however close or far;
//  2. the FARTHER mic is delayed by the extra distance / speed of sound,
//     and the delay follows whichever mic is farther - measured as real
//     spikes 60 samples apart plus a comb-filter null right where the
//     physics puts it;
//  3. proximity effect on the synthetic cab: close = much more bass than
//     far, with the highs left alone;
//  4. a slot holding a REAL IR skips the proximity shelf (its capture has
//     its own) but still takes the delay (the phase-alignment use).
//
// Real-IR tests use a single-impulse WAV so the pedal's output IS the
// delay pattern: two identical mics blended 50/50, one delayed, is
// 0.5*d(n) + 0.5*d(n - D) - spikes D apart and a null at 1/(2*D).

#include "CabPedalTestKit.h"

#include <complex>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace CabTest;

namespace
{
    double levelDb(const std::vector<float>& x, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-12));
    }

    double bandDb(const std::vector<float>& x, double lo, double hi)
    {
        double sum = 0.0;
        int count = 0;
        for (double f = lo; f <= hi; f *= 1.06, ++count)
            sum += levelDb(x, f);
        return sum / count;
    }

    // The distance knobs glide (20 ms) rather than jump, so let one full
    // render go by before measuring.
    Rendered settled(CabinetPedal& pedal)
    {
        render(pedal);
        return render(pedal);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("csim_cab_placement_test");
    dir.createDirectory();
    auto singleIR = makeIR(dir, "single.wav", { { 0, 1.0f }, { 30, 0.01f } });

    // ---- Real-IR rig: BOTH mics on the left cab carry the same single-
    // impulse IR, blended 50/50, no room. Any structure in the output is
    // therefore caused by mic placement alone. ----
    CabinetPedal rig;
    prepared(rig);
    rig.loadImpulseResponseFile(0, singleIR);
    rig.loadImpulseResponseFile(1, singleIR);
    param(rig, "Mic Blend")->set(50.0f);
    param(rig, "Room")->set(0.0f);

    Rendered r;
    waitUntil(rig, [&](const Rendered& x) { return isClean(x.left) && peakIndex(x.left) < 8; });

    std::printf("=== Equal distances never delay anything ===\n");
    {
        bool same = true;
        size_t basePeak = 0;
        for (float d : { 50.0f, 0.0f, 100.0f, 20.0f })
        {
            param(rig, "Distance A")->set(d);
            param(rig, "Distance B")->set(d);
            r = settled(rig);
            auto peak = peakIndex(r.left);
            if (d == 50.0f)
                basePeak = peak;
            same &= peak == basePeak && echoRatio(r.left, 60) < 0.05;
        }
        std::printf("  main spike stays at sample %zu for A = B = 50 / 0 / 100 / 20, with no second spike\n", basePeak);
        check(same, "A = B moves nothing, at any absolute distance");
    }

    std::printf("\n=== The farther mic is delayed by the extra distance / speed of sound ===\n");
    {
        // 1 inch vs 18 inches: 1.259 ms = 60.4 samples at 48kHz.
        param(rig, "Distance A")->set(0.0f);
        param(rig, "Distance B")->set(100.0f);
        r = settled(rig);
        auto first = peakIndex(r.left);
        auto ratio = echoRatio(r.left, 60);
        auto notch = levelDb(r.left, 397.0);   // 1 / (2 * 1.259 ms): the first comb null
        auto crest = levelDb(r.left, 794.0);   // one full period later: reinforcement
        std::printf("  B (18 in) vs A (1 in): second spike %.2f of the first, at ~60 samples\n", ratio);
        std::printf("  comb: %.1f dB at 397Hz (null) vs %.1f dB at 794Hz (peak) = %.1f dB deep\n", notch, crest, crest - notch);
        check(first < 8 && ratio > 0.5, "Mic B's copy arrives ~60 samples (1.26 ms) after Mic A's");
        check(crest - notch > 15.0, "the summed mics have a deep comb null exactly where physics says (397Hz)");

        // Swap: now Mic A is the far one. The delay must follow it.
        param(rig, "Distance A")->set(100.0f);
        param(rig, "Distance B")->set(0.0f);
        r = settled(rig);
        auto swappedRatio = echoRatio(r.left, 60);
        auto swappedNotch = levelDb(r.left, 397.0);
        std::printf("  swapped (A far, B near): second spike %.2f, null %.1f dB\n", swappedRatio, swappedNotch);
        check(swappedRatio > 0.5 && crest - swappedNotch > 15.0, "the delay follows whichever mic is farther");
    }

    std::printf("\n=== A real IR takes the delay but skips the proximity shelf ===\n");
    {
        // One real IR in Mic A only, Blend 0 -> the output is Mic A alone.
        CabinetPedal single;
        prepared(single);
        single.loadImpulseResponseFile(0, singleIR);
        param(single, "Mic Blend")->set(0.0f);
        param(single, "Room")->set(0.0f);
        waitUntil(single, [&](const Rendered& x) { return isClean(x.left) && peakIndex(x.left) < 8; });

        param(single, "Distance A")->set(0.0f);   // nearer than B (50): no delay, no shelf
        auto near = settled(single);
        param(single, "Distance A")->set(100.0f); // farther than B: delayed ~49 samples
        auto far = settled(single);

        auto shift = static_cast<int>(peakIndex(far.left)) - static_cast<int>(peakIndex(near.left));
        auto lowShift = bandDb(far.left, 100.0, 250.0) - bandDb(near.left, 100.0, 250.0);
        auto highShift = bandDb(far.left, 3000.0, 6000.0) - bandDb(near.left, 3000.0, 6000.0);
        std::printf("  Mic A 1 in -> 18 in (Mic B at 4.2 in): peak moves %d samples, 100-250Hz %+.1f dB, 3-6kHz %+.1f dB\n",
                     shift, lowShift, highShift);
        check(shift >= 46 && shift <= 52, "the delay applies to a real IR (~49 samples: 18 in vs Mic B's 4.2 in)");
        check(std::abs(lowShift) < 1.5 && std::abs(highShift) < 1.5,
              "but its tone doesn't change (a real capture already has its own proximity effect)");
    }

    std::printf("\n=== Proximity effect on the synthetic cab: close = bass ===\n");
    {
        CabinetPedal synth;
        prepared(synth);
        param(synth, "Mic Blend")->set(0.0f);
        param(synth, "Room")->set(0.0f);
        for (int i = 0; i < 4; ++i)
        {
            render(synth);
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the synthetic IRs swap in
        }

        param(synth, "Distance A")->set(50.0f);
        auto neutral = settled(synth);
        param(synth, "Distance A")->set(0.0f);
        auto close = settled(synth);
        param(synth, "Distance A")->set(100.0f);
        auto far = settled(synth);

        auto lowClose = bandDb(close.left, 100.0, 250.0) - bandDb(neutral.left, 100.0, 250.0);
        auto lowFar = bandDb(far.left, 100.0, 250.0) - bandDb(neutral.left, 100.0, 250.0);
        auto highClose = bandDb(close.left, 3000.0, 6000.0) - bandDb(neutral.left, 3000.0, 6000.0);
        auto highFar = bandDb(far.left, 3000.0, 6000.0) - bandDb(neutral.left, 3000.0, 6000.0);
        std::printf("  vs the neutral 4.2 in: 1 in -> 100-250Hz %+.1f dB, 3-6kHz %+.1f dB | 18 in -> 100-250Hz %+.1f dB, 3-6kHz %+.1f dB\n",
                     lowClose, highClose, lowFar, highFar);
        check(lowClose > 4.0, "a close mic boosts the low end");
        check(lowFar < -2.0, "a far mic has less of it");
        check(std::abs(highClose) < 1.5 && std::abs(highFar) < 1.5, "the high end doesn't move");
    }

    // ---- The synthetic cab's two DIFFERENT mics (this is what step 1's
    // weak comb-filtering finding was about): does distance give the blend
    // real notches? Metric: the deepest dip below the local (+/- 1/3
    // octave) average level, 300Hz-4kHz - the cab's own resonances give a
    // baseline amount of ripple, so it's the CHANGE that matters. ----
    std::printf("\n=== Synthetic cab, Mic A + Mic B blend: distance adds real notches ===\n");
    {
        auto deepestNotchDb = [](const std::vector<float>& x)
        {
            double worst = 0.0;
            for (double f = 300.0; f <= 4000.0; f *= 1.03)
            {
                double localSum = 0.0;
                int count = 0;
                for (double g = f / 1.26; g <= f * 1.26; g *= 1.03, ++count)
                    localSum += levelDb(x, g);
                worst = std::min(worst, levelDb(x, f) - localSum / count);
            }
            return worst;
        };

        CabinetPedal synth;
        prepared(synth);
        param(synth, "Mic Blend")->set(50.0f);
        param(synth, "Room")->set(0.0f);
        for (int i = 0; i < 4; ++i)
        {
            render(synth);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        param(synth, "Distance A")->set(50.0f);
        param(synth, "Distance B")->set(50.0f);
        auto equal = deepestNotchDb(settled(synth).left);
        param(synth, "Distance A")->set(0.0f);
        param(synth, "Distance B")->set(100.0f);
        auto apart = deepestNotchDb(settled(synth).left);
        std::printf("  deepest notch: equal distances %.1f dB, 1 in + 18 in %.1f dB (%.1f dB deeper)\n", equal, apart, equal - apart);
        check(apart < equal - 4.0, "an unequal-distance blend has clearly deeper notches than an equal one");
    }

    dir.deleteRecursively();
    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
