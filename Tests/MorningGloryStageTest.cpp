// Verify the Morning Glory model's actual claims: real harmonic content
// at moderate drive, a genuine EVEN-order harmonic from the asymmetric
// clipper (a symmetric clipper driven by a pure sine mathematically
// cannot produce one - its transfer function is odd-symmetric - so a
// clearly non-negligible 2nd harmonic directly proves the asymmetry is
// actually doing something, not just present in a comment), the Gain
// Range toggle genuinely changing drive, Boost adding low end, Bright
// Cut removing highs, the output makeup gain being real, and stability.

#include "../Source/dsp/MorningGloryStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 220.0; // A3 - a common mid-register note
    constexpr int numSamples = 8192;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: fundamental survival and real harmonic generation. ---
    std::printf("=== Sustained %.2fHz tone, Gain=0.5 ===\n", testFreq);

    MorningGloryStage stage(sampleRate);
    stage.setGain(0.5f);

    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

    stage.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, testFreq, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, testFreq * 3.0, sampleRate);

    std::printf("  fundamental (%.2fHz):  %.5f\n", testFreq, fundamental);
    std::printf("  2nd harmonic (%.2fHz): %.5f  (even order - the asymmetric-clipping signature)\n", testFreq * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz): %.5f  (odd order)\n", testFreq * 3.0, thirdHarmonic);

    bool fundamentalSurvives = fundamental > 0.1;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.005;
    std::printf("  %s: fundamental survives\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: stage generates real harmonic content\n\n", distorting ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting;

    // --- Test 2: even-harmonic signature. A clearly non-negligible 2nd
    // harmonic, well above numerical noise, is only possible because the
    // clipper is genuinely asymmetric - a symmetric tanh at the same
    // drive would produce essentially none. ---
    std::printf("=== Even-order harmonic: asymmetric clipping should produce a real 2nd harmonic ===\n");
    {
        bool hasEvenHarmonic = secondHarmonic > 0.003;
        std::printf("  2nd harmonic magnitude: %.5f (threshold 0.003)\n", secondHarmonic);
        std::printf("  %s: a real, non-negligible even-order harmonic is present\n\n", hasEvenHarmonic ? "OK" : "FAILED");
        allPassed &= hasEvenHarmonic;
    }

    // --- Test 3: Gain Range toggle - Hi should distort measurably more
    // than Lo at the same Gain knob position. ---
    std::printf("=== Gain Range: Hi should distort more than Lo at the same Gain ===\n");
    {
        auto harmonicEnergyAt = [](bool hi)
        {
            MorningGloryStage amp(sampleRate);
            amp.setGain(0.4f);
            amp.setGainRangeHi(hi);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            auto fundamentalMag = TestUtils::goertzelMagnitude(out, testFreq, sampleRate);
            auto h2 = TestUtils::goertzelMagnitude(out, testFreq * 2.0, sampleRate);
            auto h3 = TestUtils::goertzelMagnitude(out, testFreq * 3.0, sampleRate);
            return (h2 + h3) / std::max(fundamentalMag, 1.0e-6);
        };

        auto loRatio = harmonicEnergyAt(false);
        auto hiRatio = harmonicEnergyAt(true);
        std::printf("  Lo: harmonics/fundamental = %.4f\n", loRatio);
        std::printf("  Hi: harmonics/fundamental = %.4f (should be well above Lo)\n", hiRatio);

        bool rangeWorks = hiRatio > loRatio * 1.3;
        std::printf("  %s: Gain Range genuinely changes drive\n\n", rangeWorks ? "OK" : "FAILED");
        allPassed &= rangeWorks;
    }

    // --- Test 4: Boost should raise low end relative to Boost=0. ---
    std::printf("=== Boost: should raise ~100Hz relative to Boost=0 ===\n");
    {
        constexpr double bassFreq = 100.0;
        auto bassLevelAt = [bassFreq](float boostAmount)
        {
            MorningGloryStage amp(sampleRate);
            amp.setGain(0.3f);
            amp.setBoost(boostAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * bassFreq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);
            return TestUtils::goertzelMagnitude(out, bassFreq, sampleRate);
        };

        auto boostOff = bassLevelAt(0.0f);
        auto boostOn = bassLevelAt(1.0f);
        auto diffDb = TestUtils::toDb(boostOn) - TestUtils::toDb(boostOff);
        std::printf("  Boost=0: %.5f, Boost=1: %.5f (%.2fdB)\n", boostOff, boostOn, diffDb);

        bool boostWorks = diffDb > 1.0;
        std::printf("  %s: Boost raises low end\n\n", boostWorks ? "OK" : "FAILED");
        allPassed &= boostWorks;
    }

    // --- Test 5: Bright Cut should reduce high end relative to
    // BrightCut=0. ---
    std::printf("=== Bright Cut: should reduce ~5000Hz relative to BrightCut=0 ===\n");
    {
        constexpr double trebleFreq = 5000.0;
        auto trebleLevelAt = [trebleFreq](float brightCutAmount)
        {
            MorningGloryStage amp(sampleRate);
            amp.setGain(0.15f); // low gain - isolate the filter from heavy clipping's own broadband harmonics
            amp.setBrightCut(brightCutAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.2 * std::sin(2.0 * M_PI * trebleFreq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);
            return TestUtils::goertzelMagnitude(out, trebleFreq, sampleRate);
        };

        auto cutOff = trebleLevelAt(0.0f);
        auto cutOn = trebleLevelAt(1.0f);
        auto diffDb = TestUtils::toDb(cutOn) - TestUtils::toDb(cutOff);
        std::printf("  BrightCut=0: %.5f, BrightCut=1: %.5f (%.2fdB)\n", cutOff, cutOn, diffDb);

        bool brightCutWorks = diffDb < -3.0;
        std::printf("  %s: Bright Cut reduces high end\n\n", brightCutWorks ? "OK" : "FAILED");
        allPassed &= brightCutWorks;
    }

    // --- Test 6: the JFET output buffer's makeup gain should be real -
    // even near-minimum Gain should read meaningfully louder than the
    // dry input, not barely above unity. ---
    std::printf("=== Output makeup gain: even near-minimum Gain should be meaningfully above unity ===\n");
    {
        MorningGloryStage quiet(sampleRate);
        quiet.setGain(0.02f);

        constexpr double freq = 440.0;
        std::vector<float> in(numSamples), out(numSamples);
        for (int n = 0; n < numSamples; ++n)
            in[static_cast<size_t>(n)] = static_cast<float>(0.2 * std::sin(2.0 * M_PI * freq * n / sampleRate));
        quiet.processBlock(in.data(), out.data(), numSamples);

        auto inMag = TestUtils::goertzelMagnitude(in, freq, sampleRate);
        auto outMag = TestUtils::goertzelMagnitude(out, freq, sampleRate);
        auto gainDb = TestUtils::toDb(outMag) - TestUtils::toDb(inMag);
        std::printf("  in: %.5f, out: %.5f (%.2fdB)\n", inMag, outMag, gainDb);

        bool makeupGainReal = gainDb > 2.0;
        std::printf("  %s: output buffer provides real makeup gain, not just unity\n\n", makeupGainReal ? "OK" : "FAILED");
        allPassed &= makeupGainReal;
    }

    // --- Test 7: stability sweep at max everything. ---
    std::printf("=== Stability across a guitar-range sweep, max settings ===\n");
    bool rangeStable = true;

    for (double freq = 80.0; freq <= 1200.0; freq += 80.0)
    {
        MorningGloryStage sweep(sampleRate);
        sweep.setGain(1.0f);
        sweep.setGainRangeHi(true);
        sweep.setBoost(1.0f);
        sweep.setBrightCut(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweep.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz, max settings\n", freq);
                rangeStable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole sweep" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
