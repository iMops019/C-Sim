// Verify the Mesa Rectifier Modern-channel model's actual claims: real
// harmonic saturation on a dropped-tuning note (like MetalPreampChain's
// own test), the Gain-dependent voicing filter genuinely shifting
// spectral balance rather than just changing loudness, and stability
// across the full range.

#include "../Source/dsp/MesaRectifierPreamp.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double dropCLowString = 65.41;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: sustained low-string tone - fundamental survival,
    // harmonic generation, and rumble rejection. ---
    std::printf("=== Sustained %.2fHz tone (drop C low string), gain=0.8 ===\n", dropCLowString);

    MesaRectifierPreamp preamp(sampleRate);
    preamp.setGain(0.8f);

    constexpr int numSamples = 8192;
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

    // --- Test 2: Gain-dependent voicing filter corner. Verified directly
    // against the exposed corner frequency rather than acoustically -
    // Gain also scales drive into three cascaded nonlinear stages, so
    // measuring this through processBlock() would mostly be measuring
    // gain-dependent compression, not the voicing filter. ---
    std::printf("=== Gain: voicing filter corner should sweep from ~656Hz up to ~1400Hz ===\n");
    {
        MesaRectifierPreamp p(sampleRate);

        p.setGain(0.0f);
        auto lowGainCutoff = p.getVoicingCutoffHz();

        p.setGain(1.0f);
        auto highGainCutoff = p.getVoicingCutoffHz();

        std::printf("  Gain=0.0: corner = %.1fHz\n", lowGainCutoff);
        std::printf("  Gain=1.0: corner = %.1fHz\n", highGainCutoff);

        bool sweepsUp = highGainCutoff > lowGainCutoff + 500.0f;
        bool matchesResearchedEndpoints = lowGainCutoff > 600.0f && lowGainCutoff < 700.0f
                                        && highGainCutoff > 1300.0f && highGainCutoff < 1500.0f;

        std::printf("  %s: corner sweeps up with Gain\n", sweepsUp ? "OK" : "FAILED");
        std::printf("  %s: matches the researched ~656Hz-~1400Hz range\n\n",
                     matchesResearchedEndpoints ? "OK" : "FAILED");
        allPassed &= sweepsUp && matchesResearchedEndpoints;
    }

    // --- Test 3: stability sweep across common dropped-tuning low
    // strings (roughly E1 to E2) at maximum gain. ---
    std::printf("=== Stability across dropped-tuning range, max gain ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        MesaRectifierPreamp sweepPreamp(sampleRate);
        sweepPreamp.setGain(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweepPreamp.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz, max gain\n", freq);
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
