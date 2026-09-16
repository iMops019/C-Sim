// Step 7: assemble the full metal build for the first time - preamp ->
// noise gate -> tone stack -> power amp - and test the whole chain on
// the actual target signal (a palm-muted drop C chug), not just each
// module in isolation.
//
// Tone stack note: same topology as the emo build's (per the build
// order: "start with the Fender circuit - reused directly by the emo
// build, adapted for the metal build"), but with a larger mid pot range
// here - closer to a Marshall/high-gain amp's wide, dramatic mid-scoop
// capability than Fender's subtler fixed mid resistor.

#include "../Source/dsp/DiodeClipperStage.h"
#include "../Source/dsp/FenderToneStack.h"
#include "../Source/dsp/MetalPreampChain.h"
#include "../Source/dsp/NoiseGate.h"
#include "../Source/dsp/PowerAmpStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double dropCLowString = 65.41;

    FenderToneStack::Components metalToneStackComponents()
    {
        FenderToneStack::Components c;
        c.midPotOhms = 100.0e3; // wider, more Marshall-style scoop range than Fender's subtler 10k
        return c;
    }

    struct MetalAmp
    {
        MetalPreampChain preamp;
        NoiseGate gate;
        FenderToneStack toneStack;
        PowerAmpStage powerAmp;

        explicit MetalAmp(double sr)
            : preamp(sr), gate(sr), toneStack(sr, metalToneStackComponents()), powerAmp(sr)
        {
            preamp.setDrive(0.8f);
            preamp.setDiodeBlend(0.25f); // some Mesa-style edge mixed in
            gate.setThresholdDb(-45.0f);
            gate.setReleaseMs(120.0f);
            toneStack.setControls(0.6f, 0.4f, 0.7f); // present treble, tightened bass, healthy mid (5150-style growl, not scooped)
            powerAmp.setSag(0.35f);
            powerAmp.setFeedback(0.4f);
        }

        void processBlock(const float* input, float* output, int numSamples)
        {
            std::vector<float> stage1(static_cast<size_t>(numSamples));
            preamp.processBlock(input, stage1.data(), numSamples);

            std::vector<float> stage2(static_cast<size_t>(numSamples));
            gate.processBlock(stage1.data(), stage2.data(), numSamples);

            for (int n = 0; n < numSamples; ++n)
                stage2[static_cast<size_t>(n)] = toneStack.processSample(stage2[static_cast<size_t>(n)]);

            powerAmp.processBlock(stage2.data(), output, numSamples);
        }
    };
}

int main()
{
    bool allPassed = true;

    // --- Test 1: palm-muted chug - attack punches through, tail gates out ---
    std::printf("=== Full chain: palm-muted drop C chug ===\n");

    MetalAmp amp(sampleRate);

    constexpr int burstSamples = 24000; // ~500ms: attack, decay, then well into silence
    std::vector<float> chugIn(burstSamples), chugOut(burstSamples);
    for (int n = 0; n < burstSamples; ++n)
    {
        auto t = static_cast<double>(n) / sampleRate;
        auto envelope = std::exp(-t / 0.06); // tight ~60ms palm-mute decay
        chugIn[static_cast<size_t>(n)] = static_cast<float>(0.8 * envelope * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));
    }

    amp.processBlock(chugIn.data(), chugOut.data(), burstSamples);

    bool sawNonFinite = false;
    for (auto y : chugOut)
        if (! std::isfinite(y)) sawNonFinite = true;

    auto attackPeak = 0.0;
    for (int n = 0; n < static_cast<int>(0.02 * sampleRate); ++n)
        attackPeak = std::max(attackPeak, static_cast<double>(std::abs(chugOut[static_cast<size_t>(n)])));

    // Tail window: well past the ~60ms decay and the gate's ~120ms release,
    // where a real palm mute (and a working gate) should have gone quiet.
    auto tailStart = static_cast<size_t>(0.4 * sampleRate);
    auto tailPeak = 0.0;
    for (size_t n = tailStart; n < chugOut.size(); ++n)
        tailPeak = std::max(tailPeak, static_cast<double>(std::abs(chugOut[n])));

    std::printf("  attack peak (first 20ms):  %.4f\n", attackPeak);
    std::printf("  tail peak (after 400ms):   %.5f  (should be much quieter - gate + natural decay)\n", tailPeak);
    std::printf("  non-finite samples: %s\n", sawNonFinite ? "YES <-- FAIL" : "no");

    bool attackPunches = attackPeak > 0.1;
    bool tailIsQuiet = tailPeak < attackPeak * 0.05;
    allPassed &= ! sawNonFinite && attackPunches && tailIsQuiet;
    std::printf("  %s: attack punches through\n", attackPunches ? "OK" : "FAILED");
    std::printf("  %s: tail is gated down\n\n", tailIsQuiet ? "OK" : "FAILED");

    // --- Test 2: end-to-end tightness - rumble vs. fundamental through the WHOLE chain ---
    std::printf("=== End-to-end tightness: rumble vs. fundamental through the full chain ===\n");

    MetalAmp tightnessAmp(sampleRate);
    constexpr int numSamples = 8192;
    std::vector<float> sustainedIn(numSamples), sustainedOut(numSamples);
    for (int n = 0; n < numSamples; ++n)
        sustainedIn[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));

    tightnessAmp.processBlock(sustainedIn.data(), sustainedOut.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(sustainedOut, dropCLowString, sampleRate);
    auto rumble = TestUtils::goertzelMagnitude(sustainedOut, 40.0, sampleRate);

    std::printf("  fundamental: %.5f   40Hz rumble: %.5f\n", fundamental, rumble);
    bool endToEndTight = rumble < fundamental * 0.3;
    std::printf("  %s: chain stays tight end-to-end (tone stack/power amp didn't undo the preamp's work)\n\n",
                endToEndTight ? "OK" : "FAILED");
    allPassed &= endToEndTight;

    // --- Test 3: stability sweep across the dropped-tuning range through the whole chain ---
    std::printf("=== Stability across dropped-tuning range, full chain ===\n");
    bool stable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        MetalAmp sweepAmp(sampleRate);
        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweepAmp.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz\n", freq);
                stable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", stable ? "OK: stable across the whole range" : "see failures above");
    allPassed &= stable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
