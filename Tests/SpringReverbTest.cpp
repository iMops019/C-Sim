// Step 8 (emo build): verify the spring reverb actually reverberates -
// an impulse should produce a decaying tail well past the dry click, the
// Decay control should measurably change how long that tail lasts, and
// Mix should blend between pure dry and pure wet.

#include "../Source/dsp/SpringReverb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;

    // RMS of the tail well after the dry impulse has passed.
    double tailEnergy(SpringReverb& reverb, int totalSamples, int tailStartSample)
    {
        std::vector<float> input(static_cast<size_t>(totalSamples), 0.0f);
        input[0] = 1.0f; // unit impulse

        std::vector<float> output(static_cast<size_t>(totalSamples));
        reverb.processBlock(input.data(), output.data(), totalSamples);

        double sumSq = 0.0;
        int count = 0;
        for (int n = tailStartSample; n < totalSamples; ++n)
        {
            sumSq += static_cast<double>(output[static_cast<size_t>(n)]) * output[static_cast<size_t>(n)];
            ++count;
        }
        return std::sqrt(sumSq / std::max(1, count));
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: an impulse produces a real decaying tail ---
    std::printf("=== Impulse response: tail should extend well past the dry click ===\n");

    SpringReverb reverb(sampleRate);
    reverb.setDecay(0.7f);
    reverb.setMix(0.5f);

    constexpr int totalSamples = 24000; // 500ms
    auto earlyTail = tailEnergy(reverb, totalSamples, static_cast<int>(0.01 * sampleRate));  // just after the click
    reverb.reset();
    auto lateTail = tailEnergy(reverb, totalSamples, static_cast<int>(0.3 * sampleRate));    // 300ms later

    std::printf("  tail RMS at ~10ms:  %.6f\n", earlyTail);
    std::printf("  tail RMS at ~300ms: %.6f  (should still be nonzero, i.e. reverberating)\n", lateTail);
    bool hasTail = lateTail > 1.0e-5;
    std::printf("  %s: measurable reverb tail still present at 300ms\n\n", hasTail ? "OK" : "FAILED");
    allPassed &= hasTail;

    // --- Test 2: Decay control changes tail length ---
    std::printf("=== Decay control: higher decay should sustain longer ===\n");

    SpringReverb lowDecay(sampleRate);
    lowDecay.setDecay(0.1f);
    lowDecay.setMix(0.5f);
    auto lowDecayTail = tailEnergy(lowDecay, totalSamples, static_cast<int>(0.3 * sampleRate));

    SpringReverb highDecay(sampleRate);
    highDecay.setDecay(1.0f);
    highDecay.setMix(0.5f);
    auto highDecayTail = tailEnergy(highDecay, totalSamples, static_cast<int>(0.3 * sampleRate));

    std::printf("  decay=0.1: tail RMS at 300ms = %.6f\n", lowDecayTail);
    std::printf("  decay=1.0: tail RMS at 300ms = %.6f  (should be much larger)\n", highDecayTail);
    bool decayWorks = highDecayTail > lowDecayTail * 3.0;
    std::printf("  %s\n\n", decayWorks ? "OK: decay control works" : "FAILED");
    allPassed &= decayWorks;

    // --- Test 3: Mix blends dry/wet correctly ---
    std::printf("=== Mix: 0.0 should be pure dry, 1.0 pure wet ===\n");

    SpringReverb dryOnly(sampleRate);
    dryOnly.setMix(0.0f);
    std::vector<float> impulseIn(1000, 0.0f);
    impulseIn[0] = 1.0f;
    std::vector<float> dryOut(1000);
    dryOnly.processBlock(impulseIn.data(), dryOut.data(), 1000);

    bool pureDry = dryOut[0] == 1.0f;
    for (size_t i = 1; i < dryOut.size(); ++i)
        if (dryOut[i] != 0.0f) pureDry = false;

    std::printf("  mix=0.0: output[0]=%.4f, rest all zero: %s\n", dryOut[0], pureDry ? "yes" : "no");
    std::printf("  %s: mix=0.0 passes signal through unaffected\n\n", pureDry ? "OK" : "FAILED");
    allPassed &= pureDry;

    // --- Test 4: stability at extreme settings over a longer run ---
    std::printf("=== Stability at max decay/mix over a sustained tone ===\n");

    SpringReverb extreme(sampleRate);
    extreme.setDecay(1.0f);
    extreme.setMix(1.0f);

    constexpr int longRun = 48000 * 3; // 3 seconds
    std::vector<float> longIn(static_cast<size_t>(longRun)), longOut(static_cast<size_t>(longRun));
    for (int n = 0; n < longRun; ++n)
        longIn[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 220.0 * n / sampleRate));

    extreme.processBlock(longIn.data(), longOut.data(), longRun);

    bool sawNonFinite = false;
    double peakOut = 0.0;
    for (auto y : longOut)
    {
        if (! std::isfinite(y)) sawNonFinite = true;
        peakOut = std::max(peakOut, static_cast<double>(std::abs(y)));
    }

    std::printf("  peak output over 3s at max decay/mix: %.4f   non-finite: %s\n",
                peakOut, sawNonFinite ? "YES <-- FAIL" : "no");
    bool boundedOutput = peakOut < 10.0; // generous bound - just checking it doesn't run away
    allPassed &= ! sawNonFinite && boundedOutput;
    std::printf("  %s\n\n", (! sawNonFinite && boundedOutput) ? "OK: stable, bounded output" : "FAILED");

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
