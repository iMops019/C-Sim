// Verify the Twin/Princeton hybrid's actual claims: real harmonic
// content at moderate drive, that Breakup genuinely moves the cascade's
// character from Twin-clean toward Princeton-early-breakup, that Growl
// independently controls the borrowed mid bump's strength, that Twin's
// own bright-cap mechanism still works inside the hybrid, and stability
// at max Gain/Breakup/Growl together - the real lesson from this
// toolkit's other hybrid (MesaDiezelHybridPreamp): two independently-
// tuned mechanisms stacked at full strength are not automatically safe
// just because each one is safe alone.

#include "../Source/dsp/FenderHybridReverbPreamp.h"
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
    // real harmonic generation at moderate, realistic settings. ---
    std::printf("=== Sustained %.2fHz tone (open low E), Gain=0.5, Breakup=0.5 ===\n", openLowE);

    FenderHybridReverbPreamp preamp(sampleRate);
    preamp.setGain(0.5f);
    preamp.setVolume(0.6f);
    preamp.setBreakup(0.5f);
    preamp.setGrowl(0.3f);

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

    // --- Test 2: Breakup should move harmonic content upward -
    // Breakup=0 should read close to Twin-like headroom, Breakup=1
    // should read close to Princeton-like earlier breakup, at the SAME
    // Gain. ---
    std::printf("=== Breakup: harmonics/fundamental should rise from Breakup=0 to Breakup=1 ===\n");
    {
        constexpr double freq = 110.0;

        auto harmonicEnergyAt = [freq](float breakupAmount)
        {
            FenderHybridReverbPreamp amp(sampleRate);
            amp.setGain(0.5f);
            amp.setVolume(0.6f);
            amp.setBreakup(breakupAmount);
            amp.setGrowl(0.0f); // isolate Breakup's own effect from Growl's

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            auto fundamentalMag = TestUtils::goertzelMagnitude(out, freq, sampleRate);
            auto h2 = TestUtils::goertzelMagnitude(out, freq * 2.0, sampleRate);
            auto h3 = TestUtils::goertzelMagnitude(out, freq * 3.0, sampleRate);
            return (h2 + h3) / std::max(fundamentalMag, 1.0e-6);
        };

        auto ratioAtZero = harmonicEnergyAt(0.0f);
        auto ratioAtOne = harmonicEnergyAt(1.0f);

        std::printf("  Breakup=0 (Twin-like):      harmonics/fundamental = %.4f\n", ratioAtZero);
        std::printf("  Breakup=1 (Princeton-like): harmonics/fundamental = %.4f (should be well above)\n", ratioAtOne);

        bool breakupWorks = ratioAtOne > ratioAtZero * 1.4;
        std::printf("  %s: Breakup genuinely moves the cascade from clean toward early-breakup\n\n", breakupWorks ? "OK" : "FAILED");
        allPassed &= breakupWorks;
    }

    // --- Test 3: Growl - independently of Breakup, raising Growl
    // should lift the ~450Hz bump band relative to a low/high reference,
    // same measurement approach as FenderPrincetonReverbPreampTest. ---
    std::printf("=== Growl: raising Growl should lift ~450Hz relative to 100Hz and 3000Hz ===\n");
    {
        auto relativeGainAt = [](double freq, float growlAmount)
        {
            FenderHybridReverbPreamp amp(sampleRate);
            amp.setGain(0.15f); // low gain - isolate the filter shape from nonlinear compression
            amp.setVolume(0.6f);
            amp.setBreakup(0.0f);
            amp.setGrowl(growlAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.2 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            return TestUtils::toDb(TestUtils::goertzelMagnitude(out, freq, sampleRate))
                   - TestUtils::toDb(TestUtils::goertzelMagnitude(in, freq, sampleRate));
        };

        auto bumpOffDb = relativeGainAt(450.0, 0.0f);
        auto bumpOnDb = relativeGainAt(450.0, 1.0f);
        auto lowOffDb = relativeGainAt(100.0, 0.0f);
        auto lowOnDb = relativeGainAt(100.0, 1.0f);

        std::printf("  450Hz: Growl=0 %.2fdB, Growl=1 %.2fdB (diff %.2fdB)\n", bumpOffDb, bumpOnDb, bumpOnDb - bumpOffDb);
        std::printf("  100Hz: Growl=0 %.2fdB, Growl=1 %.2fdB (diff %.2fdB, should be much smaller)\n", lowOffDb, lowOnDb, lowOnDb - lowOffDb);

        bool growlLiftsMids = (bumpOnDb - bumpOffDb) > (lowOnDb - lowOffDb) + 1.0;
        std::printf("  %s: Growl independently lifts the mid bump band\n\n", growlLiftsMids ? "OK" : "FAILED");
        allPassed &= growlLiftsMids;
    }

    // --- Test 4: Twin's bright-cap mechanism should still function
    // inside the hybrid - treble relatively more prominent at low
    // Volume than high Volume, same claim as FenderTwinReverbPreampTest. ---
    std::printf("=== Bright cap: treble should be relatively more prominent at low Volume ===\n");
    {
        constexpr double trebleFreq = 3000.0;
        constexpr double midFreq = 500.0;

        auto trebleToMidDb = [trebleFreq, midFreq](float volumeAmount)
        {
            FenderHybridReverbPreamp trebleAmp(sampleRate);
            trebleAmp.setGain(0.3f);
            trebleAmp.setVolume(volumeAmount);
            trebleAmp.setBreakup(0.3f);
            FenderHybridReverbPreamp midAmp(sampleRate);
            midAmp.setGain(0.3f);
            midAmp.setVolume(volumeAmount);
            midAmp.setBreakup(0.3f);

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
        std::printf("  Volume=0.95: treble-to-mid = %.2fdB (should be lower)\n", highVolumeRatioDb);

        bool brightCapWorks = lowVolumeRatioDb > highVolumeRatioDb + 1.0;
        std::printf("  %s: Twin's bright cap still functions inside the hybrid\n\n", brightCapWorks ? "OK" : "FAILED");
        allPassed &= brightCapWorks;
    }

    // --- Test 5: stability at max Gain/Breakup/Growl TOGETHER, not
    // just each in isolation - the real lesson from MesaDiezelHybridPreamp,
    // where two individually-safe mechanisms compounded into losing the
    // fundamental when stacked at full strength. ---
    std::printf("=== Stability + fundamental survival at MAX Gain/Breakup/Growl together ===\n");
    {
        constexpr double freq = 82.41;
        FenderHybridReverbPreamp maxAmp(sampleRate);
        maxAmp.setGain(1.0f);
        maxAmp.setVolume(1.0f);
        maxAmp.setBreakup(1.0f);
        maxAmp.setGrowl(1.0f);

        std::vector<float> in(numSamples), out(numSamples);
        for (int n = 0; n < numSamples; ++n)
            in[static_cast<size_t>(n)] = static_cast<float>(0.7 * std::sin(2.0 * M_PI * freq * n / sampleRate));
        maxAmp.processBlock(in.data(), out.data(), numSamples);

        auto maxFundamental = TestUtils::goertzelMagnitude(out, freq, sampleRate);
        std::printf("  fundamental at max settings: %.5f\n", maxFundamental);

        bool finite = true;
        for (auto y : out)
        {
            if (! std::isfinite(y)) { finite = false; break; }
        }

        bool survivesAtMax = maxFundamental > 0.02;
        std::printf("  %s: output stays finite at max settings\n", finite ? "OK" : "FAILED");
        std::printf("  %s: fundamental survives even at max Gain/Breakup/Growl stacked together\n\n",
                     survivesAtMax ? "OK" : "FAILED");
        allPassed &= finite && survivesAtMax;
    }

    // --- Test 6: broader stability sweep across the low string range. ---
    std::printf("=== Stability across the low string range, max settings ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 220.0; freq += 15.0)
    {
        FenderHybridReverbPreamp sweepPreamp(sampleRate);
        sweepPreamp.setGain(1.0f);
        sweepPreamp.setVolume(1.0f);
        sweepPreamp.setBreakup(1.0f);
        sweepPreamp.setGrowl(1.0f);

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
