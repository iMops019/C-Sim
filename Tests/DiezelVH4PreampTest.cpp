// Verify the Diezel VH4 preamp model's actual claims: real harmonic
// saturation on a dropped-tuning note, a genuinely tighter/more
// bass-limited cascade than MesaRectifierPreamp at comparable gain (the
// amp's own documented "tighter... limitation hits mostly the lower
// frequencies"), the Deep control restoring that bass without touching
// the rest of the tone, and stability.

#include "../Source/dsp/DiezelVH4Preamp.h"
#include "../Source/dsp/MesaRectifierPreamp.h"
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

    DiezelVH4Preamp preamp(sampleRate);
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

    std::printf("  fundamental (%.2fHz):        %.5f\n", dropCLowString, fundamental);
    std::printf("  2nd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 3.0, thirdHarmonic);
    std::printf("  40Hz rumble (below the note): %.5f  (should be well below fundamental)\n", rumble);

    bool fundamentalSurvives = fundamental > 0.05;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.01;
    bool rumbleTight = rumble < fundamental * 0.3;

    std::printf("  %s: fundamental survives the cascade\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: cascade is generating real harmonic distortion\n", distorting ? "OK" : "FAILED");
    std::printf("  %s: sub-fundamental rumble is tightened relative to the note\n\n", rumbleTight ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting && rumbleTight;

    // --- Test 2: Diezel's cascade should carry relatively less bass
    // versus its own mids than Mesa's does versus its own mids - the
    // amp's own documented "noticeably tighter... limitation hits mostly
    // the lower frequencies." A raw magnitude comparison at one
    // frequency would also be measuring the two cascades' different
    // overall gain (four stages vs three, different makeup gains) rather
    // than just tonal balance, so this compares each amp's OWN
    // bass-to-mid ratio instead - gain-independent by construction. ---
    std::printf("=== Diezel vs. Mesa: Diezel's bass-to-mid balance should be tighter ===\n");
    {
        constexpr double bassFreq = 100.0;
        constexpr double midFreq = 1000.0;

        auto bassToMidRatioDb = [bassFreq, midFreq](auto& preamp)
        {
            std::vector<float> bassIn(numSamples), bassOut(numSamples);
            std::vector<float> midIn(numSamples), midOut(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                bassIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * bassFreq * n / sampleRate));
                midIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * midFreq * n / sampleRate));
            }
            preamp.processBlock(bassIn.data(), bassOut.data(), numSamples);
            preamp.reset();
            preamp.processBlock(midIn.data(), midOut.data(), numSamples);

            auto bassMag = TestUtils::goertzelMagnitude(bassOut, bassFreq, sampleRate);
            auto midMag = TestUtils::goertzelMagnitude(midOut, midFreq, sampleRate);
            return TestUtils::toDb(bassMag) - TestUtils::toDb(midMag);
        };

        DiezelVH4Preamp diezel(sampleRate);
        diezel.setGain(0.5f);
        diezel.setDeep(0.0f); // isolate the cascade's own tightness from Deep's restoration
        auto diezelRatioDb = bassToMidRatioDb(diezel);

        MesaRectifierPreamp mesa(sampleRate);
        mesa.setGain(0.5f);
        auto mesaRatioDb = bassToMidRatioDb(mesa);

        std::printf("  Diezel: bass-to-mid = %.2fdB\n", diezelRatioDb);
        std::printf("  Mesa:   bass-to-mid = %.2fdB (Diezel should be lower)\n", mesaRatioDb);

        bool tighter = diezelRatioDb < mesaRatioDb - 1.5;
        std::printf("  %s: Diezel carries relatively less bass than Mesa does\n\n", tighter ? "OK" : "FAILED");
        allPassed &= tighter;
    }

    // --- Test 3: Deep should restore low end without meaningfully
    // changing content elsewhere - a static shelf, not a dynamics
    // change, matching the manual's own "does not alter the dynamic
    // behavior." ---
    std::printf("=== Deep: should restore ~90Hz without changing the mids ===\n");
    {
        constexpr double bassFreq = 90.0;
        constexpr double midFreq = 1000.0;

        auto measureAt = [](float deepAmount, double freq)
        {
            DiezelVH4Preamp p(sampleRate);
            p.setGain(0.6f);
            p.setDeep(deepAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * freq * n / sampleRate));

            p.processBlock(in.data(), out.data(), numSamples);
            return TestUtils::goertzelMagnitude(out, freq, sampleRate);
        };

        auto bassOff = measureAt(0.0f, bassFreq);
        auto bassOn = measureAt(1.0f, bassFreq);
        auto bassDiffDb = TestUtils::toDb(bassOn) - TestUtils::toDb(bassOff);

        auto midOff = measureAt(0.0f, midFreq);
        auto midOn = measureAt(1.0f, midFreq);
        auto midDiffDb = TestUtils::toDb(midOn) - TestUtils::toDb(midOff);

        std::printf("  Bass (%.0fHz): Deep=0 %.4f, Deep=1 %.4f (%.2fdB)\n", bassFreq, bassOff, bassOn, bassDiffDb);
        std::printf("  Mid  (%.0fHz): Deep=0 %.4f, Deep=1 %.4f (%.2fdB)\n", midFreq, midOff, midOn, midDiffDb);

        // Deep is a simple additive-lowpass shelf, not a surgical filter
        // - some small leakage into the mids from the cascade's own
        // low-level asymmetric-clipping byproducts is an honest
        // limitation, not a bug (same reasoning as PalmMuteTamer's
        // out-of-band leakage test). The bar is "clearly smaller than
        // the intended bass change," not "mathematically zero."
        bool restoresBass = bassDiffDb > 1.5;
        bool leavesMidsAlone = std::abs(midDiffDb) < bassDiffDb * 0.4;
        std::printf("  %s: Deep restores bass\n", restoresBass ? "OK" : "FAILED");
        std::printf("  %s: Deep leaves the mids alone\n\n", leavesMidsAlone ? "OK" : "FAILED");
        allPassed &= restoresBass && leavesMidsAlone;
    }

    // --- Test 4: stability sweep across common dropped-tuning low
    // strings at maximum settings. ---
    std::printf("=== Stability across dropped-tuning range, max Gain/Deep ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        DiezelVH4Preamp sweepPreamp(sampleRate);
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
                std::printf("  FAILED: NaN/Inf at %.1fHz, max settings\n", freq);
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
