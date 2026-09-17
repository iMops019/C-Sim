// Verify the Mesa/Diezel hybrid actually combines the parts it claims
// to: real harmonic saturation, Mesa's gain-dependent voicing sweep
// still present and correct, a bass-to-mid tightness comparable to
// Diezel's own (not reverting to Mesa's looser 3-stage balance), Deep
// still restoring bass, and stability.

#include "../Source/dsp/MesaDiezelHybridPreamp.h"
#include "../Source/dsp/DiezelVH4Preamp.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double dropCLowString = 65.41;
    constexpr int numSamples = 8192;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: sustained low-string tone - fundamental survival,
    // harmonic generation, and rumble rejection. ---
    std::printf("=== Sustained %.2fHz tone (drop C low string), Gain=0.7 ===\n", dropCLowString);

    MesaDiezelHybridPreamp preamp(sampleRate);
    preamp.setGain(0.7f);
    preamp.setDeep(0.0f);

    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));

    preamp.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, dropCLowString, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 3.0, sampleRate);
    auto rumble = TestUtils::goertzelMagnitude(output, 40.0, sampleRate);

    bool fundamentalSurvives = fundamental > 0.05;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.01;
    bool rumbleTight = rumble < fundamental * 0.3;

    std::printf("  fundamental: %.5f, harmonics: %.5f, rumble: %.5f\n", fundamental, secondHarmonic + thirdHarmonic, rumble);
    std::printf("  %s: fundamental survives\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: generates real harmonic distortion\n", distorting ? "OK" : "FAILED");
    std::printf("  %s: sub-fundamental rumble is tightened\n\n", rumbleTight ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting && rumbleTight;

    // --- Test 2: Mesa's voicing shelf should still sweep correctly. ---
    std::printf("=== Mesa part: voicing corner should sweep from ~656Hz up to ~1400Hz ===\n");
    {
        MesaDiezelHybridPreamp p(sampleRate);
        p.setGain(0.0f);
        auto lowGainCutoff = p.getVoicingCutoffHz();
        p.setGain(1.0f);
        auto highGainCutoff = p.getVoicingCutoffHz();

        std::printf("  Gain=0.0: corner = %.1fHz, Gain=1.0: corner = %.1fHz\n", lowGainCutoff, highGainCutoff);

        bool sweepsCorrectly = lowGainCutoff > 600.0f && lowGainCutoff < 700.0f
                             && highGainCutoff > 1300.0f && highGainCutoff < 1500.0f;
        std::printf("  %s: Mesa's voicing sweep survived the splice\n\n", sweepsCorrectly ? "OK" : "FAILED");
        allPassed &= sweepsCorrectly;
    }

    // --- Test 3: Diezel part - the hybrid's bass-to-mid balance should
    // be comparably tight to Diezel's own, not looser like Mesa's. ---
    std::printf("=== Diezel part: hybrid's bass-to-mid balance should stay tight, like Diezel's own ===\n");
    {
        constexpr double bassFreq = 100.0;
        constexpr double midFreq = 1000.0;

        auto bassToMidRatioDb = [bassFreq, midFreq](auto& p)
        {
            std::vector<float> bassIn(numSamples), bassOut(numSamples);
            std::vector<float> midIn(numSamples), midOut(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                bassIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * bassFreq * n / sampleRate));
                midIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * midFreq * n / sampleRate));
            }
            p.processBlock(bassIn.data(), bassOut.data(), numSamples);
            p.reset();
            p.processBlock(midIn.data(), midOut.data(), numSamples);

            auto bassMag = TestUtils::goertzelMagnitude(bassOut, bassFreq, sampleRate);
            auto midMag = TestUtils::goertzelMagnitude(midOut, midFreq, sampleRate);
            return TestUtils::toDb(bassMag) - TestUtils::toDb(midMag);
        };

        MesaDiezelHybridPreamp hybrid(sampleRate);
        hybrid.setGain(0.5f);
        hybrid.setDeep(0.0f);
        auto hybridRatioDb = bassToMidRatioDb(hybrid);

        DiezelVH4Preamp diezel(sampleRate);
        diezel.setGain(0.5f);
        diezel.setDeep(0.0f);
        auto diezelRatioDb = bassToMidRatioDb(diezel);

        std::printf("  Hybrid: bass-to-mid = %.2fdB\n", hybridRatioDb);
        std::printf("  Diezel: bass-to-mid = %.2fdB (should be comparable)\n", diezelRatioDb);

        bool comparablyTight = std::abs(hybridRatioDb - diezelRatioDb) < 4.0;
        std::printf("  %s: hybrid keeps Diezel's tightness, doesn't revert to a looser balance\n\n",
                     comparablyTight ? "OK" : "FAILED");
        allPassed &= comparablyTight;
    }

    // --- Test 4: Deep should still restore bass. ---
    std::printf("=== Deep: should still restore ~90Hz ===\n");
    {
        constexpr double bassFreq = 90.0;

        auto magnitudeAtDeep = [bassFreq](float deepAmount)
        {
            MesaDiezelHybridPreamp p(sampleRate);
            p.setGain(0.6f);
            p.setDeep(deepAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * bassFreq * n / sampleRate));

            p.processBlock(in.data(), out.data(), numSamples);
            return TestUtils::goertzelMagnitude(out, bassFreq, sampleRate);
        };

        auto off = magnitudeAtDeep(0.0f);
        auto on = magnitudeAtDeep(1.0f);
        auto diffDb = TestUtils::toDb(on) - TestUtils::toDb(off);

        std::printf("  Deep=0.0: %.4f, Deep=1.0: %.4f (%.2fdB)\n", off, on, diffDb);

        bool restoresBass = diffDb > 1.5;
        std::printf("  %s: Deep still restores bass in the hybrid\n\n", restoresBass ? "OK" : "FAILED");
        allPassed &= restoresBass;
    }

    // --- Test 5: stability sweep. ---
    std::printf("=== Stability across dropped-tuning range, max settings ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        MesaDiezelHybridPreamp sweepPreamp(sampleRate);
        sweepPreamp.setGain(1.0f);
        sweepPreamp.setDeep(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweepPreamp.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz\n", freq);
                rangeStable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole dropped-tuning range" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
