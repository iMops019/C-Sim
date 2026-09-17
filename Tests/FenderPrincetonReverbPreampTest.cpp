// Verify the Princeton Reverb preamp model's actual claims: real
// harmonic content at moderate drive, genuinely EARLIER breakup than
// FenderTwinReverbPreamp at the same Gain setting - the amp's own
// documented "abundant overdrive capability" and earlier-breakup
// character versus the Twin's headroom - a fixed mid-focused voicing
// bump ("browner... mid-focused tone"), and stability.

#include "../Source/dsp/FenderPrincetonReverbPreamp.h"
#include "../Source/dsp/FenderTwinReverbPreamp.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double openLowE = 82.41;
    constexpr int numSamples = 8192;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: sustained open-low-E tone - fundamental survival and
    // real harmonic generation at a moderate, realistic Gain. ---
    std::printf("=== Sustained %.2fHz tone (open low E), Gain=0.5, Volume=0.7 ===\n", openLowE);

    FenderPrincetonReverbPreamp preamp(sampleRate);
    preamp.setGain(0.5f);
    preamp.setVolume(0.7f);

    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * openLowE * n / sampleRate));

    preamp.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, openLowE, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, openLowE * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, openLowE * 3.0, sampleRate);

    std::printf("  fundamental (%.2fHz):   %.5f\n", openLowE, fundamental);
    std::printf("  2nd harmonic (%.2fHz):  %.5f  (evidence of distortion)\n", openLowE * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz):  %.5f  (evidence of distortion)\n", openLowE * 3.0, thirdHarmonic);

    bool fundamentalSurvives = fundamental > 0.1;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.002;
    std::printf("  %s: fundamental survives the cascade\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: cascade generates real harmonic content\n\n", distorting ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting;

    // --- Test 2: earlier breakup - at the SAME Gain setting, Princeton
    // Reverb should generate meaningfully MORE harmonic distortion than
    // FenderTwinReverbPreamp - the direct inverse of the Twin's own
    // headroom test, both amps' sourced defining traits. ---
    std::printf("=== Breakup: Princeton Reverb should distort more than Twin Reverb at the same Gain ===\n");
    {
        constexpr double freq = 110.0;

        auto harmonicEnergyAt = [freq](auto& amp)
        {
            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            auto fundamentalMag = TestUtils::goertzelMagnitude(out, freq, sampleRate);
            auto h2 = TestUtils::goertzelMagnitude(out, freq * 2.0, sampleRate);
            auto h3 = TestUtils::goertzelMagnitude(out, freq * 3.0, sampleRate);
            return (h2 + h3) / std::max(fundamentalMag, 1.0e-6);
        };

        FenderPrincetonReverbPreamp princeton(sampleRate);
        princeton.setGain(0.5f);
        princeton.setVolume(0.7f);
        auto princetonRatio = harmonicEnergyAt(princeton);

        FenderTwinReverbPreamp twin(sampleRate);
        twin.setGain(0.5f);
        twin.setVolume(0.6f);
        auto twinRatio = harmonicEnergyAt(twin);

        std::printf("  Princeton Reverb: harmonics/fundamental = %.4f\n", princetonRatio);
        std::printf("  Twin Reverb:      harmonics/fundamental = %.4f (Princeton should be well above this)\n", twinRatio);

        bool breaksUpEarlier = princetonRatio > twinRatio * 1.5;
        std::printf("  %s: Princeton Reverb breaks up earlier than Twin Reverb at the same Gain\n\n", breaksUpEarlier ? "OK" : "FAILED");
        allPassed &= breaksUpEarlier;
    }

    // --- Test 3: mid-focused voicing bump - a note in the fixed bump's
    // ~300-650Hz band should read relatively louder against a clean
    // pass-through reference than a note well outside that band (e.g.
    // well below or well above), evidencing the fixed "browner...
    // mid-focused" bump actually shapes the tone as claimed. ---
    std::printf("=== Mid-focus: the fixed low-mid bump should lift ~450Hz relative to 100Hz and 3000Hz ===\n");
    {
        constexpr double bumpFreq = 450.0;
        constexpr double lowFreq = 100.0;
        constexpr double highFreq = 3000.0;

        auto gainAt = [](double freq)
        {
            FenderPrincetonReverbPreamp amp(sampleRate);
            amp.setGain(0.15f); // low gain - isolate the fixed filter shape from nonlinear compression
            amp.setVolume(0.7f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.2 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            return TestUtils::toDb(TestUtils::goertzelMagnitude(out, freq, sampleRate))
                   - TestUtils::toDb(TestUtils::goertzelMagnitude(in, freq, sampleRate));
        };

        auto bumpGainDb = gainAt(bumpFreq);
        auto lowGainDb = gainAt(lowFreq);
        auto highGainDb = gainAt(highFreq);

        std::printf("  Relative gain at 100Hz:  %.2fdB\n", lowGainDb);
        std::printf("  Relative gain at 450Hz:  %.2fdB (should be the highest of the three)\n", bumpGainDb);
        std::printf("  Relative gain at 3000Hz: %.2fdB\n", highGainDb);

        bool midIsBumped = bumpGainDb > lowGainDb + 0.5 && bumpGainDb > highGainDb + 0.5;
        std::printf("  %s: the fixed mid bump measurably favours ~450Hz\n\n", midIsBumped ? "OK" : "FAILED");
        allPassed &= midIsBumped;
    }

    // --- Test 4: stability sweep across the guitar's low range at max
    // settings. ---
    std::printf("=== Stability across the low string range, max Gain/Volume ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 220.0; freq += 15.0)
    {
        FenderPrincetonReverbPreamp sweepPreamp(sampleRate);
        sweepPreamp.setGain(1.0f);
        sweepPreamp.setVolume(1.0f);

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
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole low-string range" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
