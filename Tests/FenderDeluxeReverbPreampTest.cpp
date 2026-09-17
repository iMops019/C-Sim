// Verify the Deluxe Reverb preamp model's actual claims: real harmonic
// content at moderate drive, that its distortion at a given Gain sits
// genuinely BETWEEN FenderPrincetonReverbPreamp (early, browner breakup)
// and FenderTwinReverbPreamp (stays clean) - the amp's own well-known
// "breaks up nicely when you push it" reputation, neither sibling's
// extreme - the mid-cascade Volume/bright-cap mechanism still working,
// and stability.

#include "../Source/dsp/FenderDeluxeReverbPreamp.h"
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
    // real harmonic generation at moderate Gain. ---
    std::printf("=== Sustained %.2fHz tone (open low E), Gain=0.5, Volume=0.6 ===\n", openLowE);

    FenderDeluxeReverbPreamp preamp(sampleRate);
    preamp.setGain(0.5f);
    preamp.setVolume(0.6f);

    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * openLowE * n / sampleRate));

    preamp.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, openLowE, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, openLowE * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, openLowE * 3.0, sampleRate);

    std::printf("  fundamental (%.2fHz):   %.5f\n", openLowE, fundamental);
    std::printf("  2nd harmonic (%.2fHz):  %.5f\n", openLowE * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz):  %.5f\n", openLowE * 3.0, thirdHarmonic);

    bool fundamentalSurvives = fundamental > 0.1;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.002;
    std::printf("  %s: fundamental survives the cascade\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: cascade generates real harmonic content\n\n", distorting ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting;

    // --- Test 2: the headroom middle-ground claim - at the SAME Gain,
    // Deluxe Reverb's distortion should sit strictly between Princeton
    // (most) and Twin (least). ---
    std::printf("=== Middle ground: Twin < Deluxe < Princeton in distortion, at the same Gain ===\n");
    {
        constexpr double freq = 110.0;

        auto harmonicRatioAt = [freq](auto& amp)
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

        FenderTwinReverbPreamp twin(sampleRate);
        twin.setGain(0.5f);
        twin.setVolume(0.6f);
        auto twinRatio = harmonicRatioAt(twin);

        FenderDeluxeReverbPreamp deluxe(sampleRate);
        deluxe.setGain(0.5f);
        deluxe.setVolume(0.6f);
        auto deluxeRatio = harmonicRatioAt(deluxe);

        FenderPrincetonReverbPreamp princeton(sampleRate);
        princeton.setGain(0.5f);
        princeton.setVolume(0.7f);
        auto princetonRatio = harmonicRatioAt(princeton);

        std::printf("  Twin Reverb:      harmonics/fundamental = %.4f (should be lowest)\n", twinRatio);
        std::printf("  Deluxe Reverb:    harmonics/fundamental = %.4f (should be in the middle)\n", deluxeRatio);
        std::printf("  Princeton Reverb: harmonics/fundamental = %.4f (should be highest)\n", princetonRatio);

        bool middleGround = (deluxeRatio > twinRatio) && (deluxeRatio < princetonRatio);
        std::printf("  %s: Deluxe Reverb sits genuinely between its two siblings\n\n", middleGround ? "OK" : "FAILED");
        allPassed &= middleGround;
    }

    // --- Test 3: bright cap / Volume interaction, same claim as
    // FenderTwinReverbPreampTest. ---
    std::printf("=== Bright cap: treble should be relatively more prominent at low Volume ===\n");
    {
        constexpr double trebleFreq = 3000.0;
        constexpr double midFreq = 500.0;

        auto trebleToMidDb = [trebleFreq, midFreq](float volumeAmount)
        {
            FenderDeluxeReverbPreamp amp(sampleRate);
            amp.setGain(0.3f);
            amp.setVolume(volumeAmount);

            std::vector<float> trebleIn(numSamples), trebleOut(numSamples);
            std::vector<float> midIn(numSamples), midOut(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                trebleIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * trebleFreq * n / sampleRate));
                midIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * midFreq * n / sampleRate));
            }
            amp.processBlock(trebleIn.data(), trebleOut.data(), numSamples);
            amp.reset();
            amp.processBlock(midIn.data(), midOut.data(), numSamples);

            auto trebleMag = TestUtils::goertzelMagnitude(trebleOut, trebleFreq, sampleRate);
            auto midMag = TestUtils::goertzelMagnitude(midOut, midFreq, sampleRate);
            return TestUtils::toDb(trebleMag) - TestUtils::toDb(midMag);
        };

        auto lowVolumeRatioDb = trebleToMidDb(0.15f);
        auto highVolumeRatioDb = trebleToMidDb(0.95f);

        std::printf("  Volume=0.15: treble-to-mid = %.2fdB\n", lowVolumeRatioDb);
        std::printf("  Volume=0.95: treble-to-mid = %.2fdB (should be lower)\n", highVolumeRatioDb);

        bool brightCapWorks = lowVolumeRatioDb > highVolumeRatioDb + 1.0;
        std::printf("  %s: bright cap lifts treble more at low Volume than high Volume\n\n", brightCapWorks ? "OK" : "FAILED");
        allPassed &= brightCapWorks;
    }

    // --- Test 4: stability sweep across the low string range at max
    // settings. ---
    std::printf("=== Stability across the low string range, max Gain/Volume ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 220.0; freq += 15.0)
    {
        FenderDeluxeReverbPreamp sweepPreamp(sampleRate);
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
