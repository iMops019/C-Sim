// Verify the Twin Reverb preamp model's actual claims: real harmonic
// content at moderate drive, genuinely more headroom (less distortion
// at a comparable Gain setting) than this toolkit's high-gain preamps
// - the amp's own documented "stays clean up to almost 6 [on the
// dial]... designed not to break up like the other Fender amps" - the
// mid-cascade Volume control's bright-cap interaction (stronger treble
// lift at low Volume, fading at high Volume), and stability.

#include "../Source/dsp/FenderTwinReverbPreamp.h"
#include "../Source/dsp/MesaRectifierPreamp.h"
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
    std::printf("=== Sustained %.2fHz tone (open low E), Gain=0.6, Volume=0.6 ===\n", openLowE);

    FenderTwinReverbPreamp preamp(sampleRate);
    preamp.setGain(0.6f);
    preamp.setVolume(0.6f);

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
    std::printf("  %s: cascade generates real (if modest) harmonic content\n\n", distorting ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting;

    // --- Test 2: headroom - at the SAME Gain setting, Twin Reverb
    // should generate meaningfully less harmonic distortion than
    // MesaRectifierPreamp - the amp's own sourced "stays clean...
    // other [amps] break up" character, not just quieter output (both
    // preamps normalise their own output level internally, so this is
    // a genuine distortion-amount comparison, not a level artifact). ---
    std::printf("=== Headroom: Twin Reverb should distort far less than Mesa at the same Gain ===\n");
    {
        constexpr double freq = 110.0; // A2, a common chord-tone fundamental

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

        FenderTwinReverbPreamp twin(sampleRate);
        twin.setGain(0.5f);
        twin.setVolume(0.6f);
        auto twinRatio = harmonicEnergyAt(twin);

        MesaRectifierPreamp mesa(sampleRate);
        mesa.setGain(0.5f);
        auto mesaRatio = harmonicEnergyAt(mesa);

        std::printf("  Twin Reverb: harmonics/fundamental = %.4f\n", twinRatio);
        std::printf("  Mesa:        harmonics/fundamental = %.4f (Twin should be well below this)\n", mesaRatio);

        bool cleaner = twinRatio < mesaRatio * 0.5;
        std::printf("  %s: Twin Reverb has genuinely more headroom than Mesa at the same Gain\n\n", cleaner ? "OK" : "FAILED");
        allPassed &= cleaner;
    }

    // --- Test 3: bright cap - a high-frequency note should read
    // relatively louder (vs its own low-Volume baseline) at low Volume
    // than at high Volume, matching the real bright-cap-across-the-pot
    // behaviour (strongest when the pot loads the signal most, i.e. at
    // low settings). Comparing relative brightness at two Volume
    // settings avoids the Goertzel scalloping-loss pitfall entirely,
    // since both measurements use the same frequency/block length. ---
    std::printf("=== Bright cap: treble should be relatively more prominent at low Volume ===\n");
    {
        constexpr double trebleFreq = 3000.0;
        constexpr double midFreq = 500.0;

        auto trebleToMidDb = [trebleFreq, midFreq](float volumeAmount)
        {
            FenderTwinReverbPreamp trebleAmp(sampleRate);
            trebleAmp.setGain(0.3f);
            trebleAmp.setVolume(volumeAmount);
            FenderTwinReverbPreamp midAmp(sampleRate);
            midAmp.setGain(0.3f);
            midAmp.setVolume(volumeAmount);

            std::vector<float> trebleIn(numSamples), trebleOut(numSamples);
            std::vector<float> midIn(numSamples), midOut(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                trebleIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * trebleFreq * n / sampleRate));
                midIn[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * midFreq * n / sampleRate));
            }
            trebleAmp.processBlock(trebleIn.data(), trebleOut.data(), numSamples);
            midAmp.processBlock(midIn.data(), midOut.data(), numSamples);

            auto trebleMag = TestUtils::goertzelMagnitude(trebleOut, trebleFreq, sampleRate);
            auto midMag = TestUtils::goertzelMagnitude(midOut, midFreq, sampleRate);
            return TestUtils::toDb(trebleMag) - TestUtils::toDb(midMag);
        };

        auto lowVolumeRatioDb = trebleToMidDb(0.15f);
        auto highVolumeRatioDb = trebleToMidDb(0.95f);

        std::printf("  Volume=0.15: treble-to-mid = %.2fdB\n", lowVolumeRatioDb);
        std::printf("  Volume=0.95: treble-to-mid = %.2fdB (should be lower - bright cap fades)\n", highVolumeRatioDb);

        bool brightCapFades = lowVolumeRatioDb > highVolumeRatioDb + 1.0;
        std::printf("  %s: bright cap lifts treble more at low Volume than high Volume\n\n", brightCapFades ? "OK" : "FAILED");
        allPassed &= brightCapFades;
    }

    // --- Test 4: stability sweep across the guitar's low range at max
    // settings. ---
    std::printf("=== Stability across the low string range, max Gain/Volume ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 220.0; freq += 15.0)
    {
        FenderTwinReverbPreamp sweepPreamp(sampleRate);
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
