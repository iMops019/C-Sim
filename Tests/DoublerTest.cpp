// Verify the doubler's actual claim: Detune should make the wet voice's
// pitch audibly drift over time (not just filter the tone), Mix should
// blend cleanly between dry and wet, and it should stay stable.

#include "../Source/dsp/Doubler.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 196.0; // G3-ish

    // Time (seconds) between successive rising zero-crossings - a direct
    // measurement of instantaneous period, which should be rock-steady
    // for an undetuned signal and visibly wobble once the delay is
    // modulated (a modulated delay is a Doppler/pitch shift).
    std::vector<double> zeroCrossingPeriods(const std::vector<float>& signal)
    {
        std::vector<double> periods;
        long lastCrossing = -1;
        for (size_t n = 1; n < signal.size(); ++n)
        {
            if (signal[n - 1] <= 0.0f && signal[n] > 0.0f)
            {
                if (lastCrossing >= 0)
                    periods.push_back(static_cast<double>(n - static_cast<size_t>(lastCrossing)) / sampleRate);
                lastCrossing = static_cast<long>(n);
            }
        }
        return periods;
    }

    double variance(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double mean = 0.0;
        for (auto v : values) mean += v;
        mean /= static_cast<double>(values.size());

        double sumSq = 0.0;
        for (auto v : values) sumSq += (v - mean) * (v - mean);
        return sumSq / static_cast<double>(values.size());
    }

    std::vector<float> runDoubler(float detune, float rateHz, float mix, int numSamples)
    {
        Doubler doubler(sampleRate);
        doubler.setDetune(detune);
        doubler.setRateHz(rateHz);
        doubler.setMix(mix);

        std::vector<float> input(static_cast<size_t>(numSamples)), output(static_cast<size_t>(numSamples));
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = 0.6f * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        doubler.processBlock(input.data(), output.data(), numSamples);
        return output;
    }
}

int main()
{
    bool allPassed = true;

    constexpr int numSamples = static_cast<int>(sampleRate * 4); // 4s - several LFO cycles even at a slow rate

    // --- Test 1: Mix=0 is exact dry passthrough ---
    std::printf("=== Mix=0: should pass the dry signal through unaffected ===\n");
    {
        Doubler doubler(sampleRate);
        doubler.setDetune(1.0f);
        doubler.setMix(0.0f);

        std::vector<float> input(2000), output(2000);
        for (int n = 0; n < 2000; ++n)
            input[static_cast<size_t>(n)] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        doubler.processBlock(input.data(), output.data(), 2000);

        bool exact = true;
        for (size_t n = 0; n < input.size(); ++n)
            if (std::abs(input[n] - output[n]) > 1.0e-6f) exact = false;

        std::printf("  %s: mix=0 output matches input exactly\n\n", exact ? "OK" : "FAILED");
        allPassed &= exact;
    }

    // A faster LFO rate for these two tests makes the delay's rate of
    // change (and so the resulting frequency deviation) larger and easier
    // to measure reliably in a few seconds of audio; Test 4 below covers
    // stability at this same rate.
    constexpr float testRateHz = 5.0f;

    // --- Test 2: Detune=0 wet path is a fixed delay (steady pitch) ---
    std::printf("=== Detune=0: wet-only output should hold a rock-steady pitch ===\n");
    {
        auto out = runDoubler(0.0f, testRateHz, 1.0f, numSamples);
        auto periods = zeroCrossingPeriods(out);
        // Skip the first few periods (buffer fill/startup).
        std::vector<double> steady(periods.begin() + 20, periods.end());
        auto var = variance(steady);
        std::printf("  period variance at detune=0: %.3e (expect near numerical noise floor)\n", var);
        bool steadyPitch = var < 1.0e-9;
        std::printf("  %s\n\n", steadyPitch ? "OK" : "FAILED");
        allPassed &= steadyPitch;
    }

    // --- Test 3: Detune>0 makes the pitch audibly wobble ---
    std::printf("=== Detune=1: wet-only output's period should visibly vary over time ===\n");
    {
        auto out = runDoubler(1.0f, testRateHz, 1.0f, numSamples);
        auto periods = zeroCrossingPeriods(out);
        std::vector<double> steady(periods.begin() + 20, periods.end());
        auto var = variance(steady);
        std::printf("  period variance at detune=1: %.3e (expect orders of magnitude above the detune=0 case)\n", var);
        bool wobbles = var > 1.0e-8;
        std::printf("  %s\n\n", wobbles ? "OK: detune produces real pitch drift" : "FAILED");
        allPassed &= wobbles;
    }

    // --- Test 4: stability over a long run at max settings ---
    std::printf("=== Stability at max detune/mix over a sustained tone ===\n");
    {
        auto out = runDoubler(1.0f, 5.0f, 1.0f, static_cast<int>(sampleRate * 3));
        bool sawNonFinite = false;
        double peak = 0.0;
        for (auto y : out)
        {
            if (!std::isfinite(y)) sawNonFinite = true;
            peak = std::max(peak, static_cast<double>(std::abs(y)));
        }
        std::printf("  peak output: %.4f, non-finite: %s\n", peak, sawNonFinite ? "YES <-- FAIL" : "no");
        bool stable = !sawNonFinite && peak < 5.0;
        std::printf("  %s\n\n", stable ? "OK: stable, bounded output" : "FAILED");
        allPassed &= stable;
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
