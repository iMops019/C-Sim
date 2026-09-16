// Step 8 (emo build): verify the tremolo actually modulates amplitude at
// the set rate, and that depth=0 gives no modulation. (The bias-vs-plain-
// AM distinction is architectural - modulating grid bias inherently
// varies harmonic content along with gain, since it moves the operating
// point on a nonlinear curve - not separately measured here.)

#include "../Source/dsp/BiasModulatedTremolo.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double audioFreq = 480.0;

    // Rectified + smoothed envelope of a signal. Cascades the one-pole
    // smoother twice at a low cutoff (steeper rolloff, further below the
    // audio rate) rather than a single gentle pole: a single pole at
    // 50Hz only attenuates a 480Hz tone by ~20dB, which was leaving a
    // visible residual ripple that swamped the much smaller true
    // amplitude-modulation signal at 5-9Hz and looked like a bug in the
    // tremolo itself (confirmed by testing the oversampled Koren stage
    // directly with a hardcoded constant bias and no LFO at all - same
    // "phantom modulation" appeared, so it was the envelope extraction,
    // not the model).
    std::vector<float> envelopeOf(const std::vector<float>& signal, double smoothingHz, double sr)
    {
        std::vector<float> env(signal.size());
        auto coeff = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * smoothingHz / sr));
        float state1 = 0.0f, state2 = 0.0f;
        for (size_t i = 0; i < signal.size(); ++i)
        {
            state1 += coeff * (std::abs(signal[i]) - state1);
            state2 += coeff * (state1 - state2);
            env[i] = state2;
        }
        return env;
    }

    // Drops the envelope-follower's own startup transient (a brief,
    // broadband event that otherwise leaks a little energy into every
    // Goertzel bin, including whatever rate we're testing for).
    std::vector<float> trimStart(const std::vector<float>& v, size_t skip)
    {
        return std::vector<float>(v.begin() + static_cast<long>(skip), v.end());
    }

    // Standard deviation of an (already-settled) envelope - a direct,
    // frequency-agnostic measure of "how much does this vary", used to
    // sidestep Goertzel's baseline sensitivity when comparing across
    // signals with different overall envelope statistics (rather than
    // within one signal, where the rate-tracking test below uses it fine).
    double envelopeVariation(const std::vector<float>& env)
    {
        double mean = 0.0;
        for (auto v : env) mean += v;
        mean /= static_cast<double>(env.size());

        double variance = 0.0;
        for (auto v : env) variance += (v - mean) * (v - mean);
        variance /= static_cast<double>(env.size());

        return std::sqrt(variance);
    }
}

int main()
{
    bool allPassed = true;

    constexpr int numSamples = 96000; // 2 seconds
    std::vector<float> input(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * audioFreq * n / sampleRate));

    // --- Test 1: depth > 0 modulates amplitude at the set rate ---
    std::printf("=== Tremolo modulation at set rate ===\n");

    BiasModulatedTremolo tremolo(sampleRate);
    tremolo.setRateHz(5.0f);
    tremolo.setDepth(0.8f);

    std::vector<float> output(numSamples);
    tremolo.processBlock(input.data(), output.data(), numSamples);

    auto envelope = trimStart(envelopeOf(output, 15.0, sampleRate), 7200);
    auto modulationDepth = envelopeVariation(envelope);

    std::printf("  envelope std deviation, depth=0.8: %.5f\n", modulationDepth);
    bool modulates = modulationDepth > 0.02;
    std::printf("  %s: measurable amplitude modulation\n\n", modulates ? "OK" : "FAILED");
    allPassed &= modulates;

    // --- Test 2: depth = 0 gives no modulation ---
    std::printf("=== Depth=0 gives no modulation ===\n");

    BiasModulatedTremolo noDepth(sampleRate);
    noDepth.setRateHz(5.0f);
    noDepth.setDepth(0.0f);

    std::vector<float> noDepthOut(numSamples);
    noDepth.processBlock(input.data(), noDepthOut.data(), numSamples);

    auto noDepthEnvelope = trimStart(envelopeOf(noDepthOut, 15.0, sampleRate), 7200);
    auto noDepthVariation = envelopeVariation(noDepthEnvelope);

    std::printf("  envelope std deviation, depth=0.0: %.6f  (should be much smaller than depth=0.8's %.5f)\n",
                noDepthVariation, modulationDepth);
    bool depthZeroFlat = noDepthVariation < modulationDepth * 0.1;
    std::printf("  %s\n\n", depthZeroFlat ? "OK: depth=0 stays flat" : "FAILED");
    allPassed &= depthZeroFlat;

    // --- Test 3: rate control - changing rate moves the modulation frequency ---
    std::printf("=== Rate control: modulation should track a different rate ===\n");

    BiasModulatedTremolo fastTremolo(sampleRate);
    fastTremolo.setRateHz(9.0f);
    fastTremolo.setDepth(0.8f);
    std::vector<float> fastOut(numSamples);
    fastTremolo.processBlock(input.data(), fastOut.data(), numSamples);
    auto fastEnvelope = envelopeOf(fastOut, 15.0, sampleRate);

    auto trimmedFastEnvelope = trimStart(fastEnvelope, 7200);
    auto energyAt5 = TestUtils::goertzelMagnitude(trimmedFastEnvelope, 5.0, sampleRate);
    auto energyAt9 = TestUtils::goertzelMagnitude(trimmedFastEnvelope, 9.0, sampleRate);

    std::printf("  rate=9Hz: envelope energy at 5Hz=%.5f, at 9Hz=%.5f (9Hz should dominate)\n", energyAt5, energyAt9);
    bool rateTracks = energyAt9 > energyAt5;
    std::printf("  %s\n\n", rateTracks ? "OK: modulation frequency tracks the Rate control" : "FAILED");
    allPassed &= rateTracks;

    // --- Test 4: stability ---
    std::printf("=== Stability at extreme settings ===\n");
    BiasModulatedTremolo extreme(sampleRate);
    extreme.setRateHz(20.0f);
    extreme.setDepth(1.0f);
    std::vector<float> extremeOut(numSamples);
    extreme.processBlock(input.data(), extremeOut.data(), numSamples);

    bool sawNonFinite = false;
    for (auto y : extremeOut)
        if (! std::isfinite(y)) sawNonFinite = true;
    std::printf("  %s\n\n", sawNonFinite ? "FAILED: NaN/Inf" : "OK: stable at max rate/depth");
    allPassed &= ! sawNonFinite;

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
