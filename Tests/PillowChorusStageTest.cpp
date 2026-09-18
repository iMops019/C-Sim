// Verify the Pillow Chorus model's actual claims: Chorus=0 is a clean
// passthrough, Chorus>0 makes the pitch measurably wobble (same
// zero-crossing-period-variance technique as DoublerTest, since this is
// the same "modulated delay = Doppler pitch drift" mechanism), the
// effect stays genuinely light even at Chorus=1 (bounded wet mix, small
// depth - the whole "soft mellow pillow" spec, not just "some chorus
// happens"), and it stays stable.

#include "../Source/dsp/PillowChorusStage.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 196.0; // G3-ish

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

    std::vector<float> runChorus(float chorus, int numSamples)
    {
        PillowChorusStage stage(sampleRate);
        stage.setChorus(chorus);

        std::vector<float> input(static_cast<size_t>(numSamples)), output(static_cast<size_t>(numSamples));
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = 0.6f * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        stage.processBlock(input.data(), output.data(), numSamples);
        return output;
    }
}

int main()
{
    bool allPassed = true;

    constexpr int numSamples = static_cast<int>(sampleRate * 6); // several LFO cycles even at the slow end of the range

    // --- Test 1: Chorus=0 is exact dry passthrough. ---
    std::printf("=== Chorus=0: should pass the dry signal through unaffected ===\n");
    {
        PillowChorusStage stage(sampleRate);
        stage.setChorus(0.0f);

        std::vector<float> input(2000), output(2000);
        for (int n = 0; n < 2000; ++n)
            input[static_cast<size_t>(n)] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        stage.processBlock(input.data(), output.data(), 2000);

        bool exact = true;
        for (size_t n = 0; n < input.size(); ++n)
            if (std::abs(input[n] - output[n]) > 1.0e-6f) exact = false;

        std::printf("  %s: chorus=0 output matches input exactly\n\n", exact ? "OK" : "FAILED");
        allPassed &= exact;
    }

    // --- Test 2/3: Chorus=1 makes the pitch measurably wobble relative
    // to Chorus=0's own noise floor. Because the mix is deliberately
    // capped well under full-wet (the "light, not lush" spec - see Test
    // 4), the absolute wobble is intentionally modest compared to, say,
    // Doubler's full-wet test - so the real signature to check is a
    // clear relative separation from the chorus=0 floor, not a large
    // absolute number. ---
    std::printf("=== Chorus=1 vs Chorus=0: period variance should separate clearly ===\n");
    {
        auto wobbleVariance = variance([&] {
            auto periods = zeroCrossingPeriods(runChorus(1.0f, numSamples));
            return std::vector<double>(periods.begin() + 20, periods.end());
        }());
        auto floorVariance = variance([&] {
            auto periods = zeroCrossingPeriods(runChorus(0.0f, numSamples));
            return std::vector<double>(periods.begin() + 20, periods.end());
        }());

        std::printf("  period variance at chorus=0: %.3e (noise floor)\n", floorVariance);
        std::printf("  period variance at chorus=1: %.3e\n", wobbleVariance);

        bool wobbles = wobbleVariance > floorVariance * 5.0;
        std::printf("  %s: chorus produces real, knob-driven pitch drift\n\n", wobbles ? "OK" : "FAILED");
        allPassed &= wobbles;
    }

    // --- Test 4: the "light/subtle" spec itself - even at Chorus=1, the
    // wet mix should never dominate the dry signal (a real chorus at max
    // depth/mix would sound obviously "doubled", not a light shimmer). ---
    std::printf("=== Light-chorus spec: even at Chorus=1, wet level should stay clearly under the dry level ===\n");
    {
        PillowChorusStage stage(sampleRate);
        stage.setChorus(1.0f);

        constexpr int n2 = 4096;
        std::vector<float> in(n2), out(n2);
        for (int n = 0; n < n2; ++n)
            in[static_cast<size_t>(n)] = 0.6f * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));
        stage.processBlock(in.data(), out.data(), n2);

        // Since dry+wet mix at Chorus=1 is input*(1-mix) + wet*mix with
        // mix capped well under 0.5, the output's overall level should
        // stay close to the dry level, not swing wildly toward the wet
        // voice - a coarse but real proxy for "light, not lush."
        double inRms = 0.0, outRms = 0.0;
        for (int n = 0; n < n2; ++n)
        {
            inRms += static_cast<double>(in[static_cast<size_t>(n)]) * in[static_cast<size_t>(n)];
            outRms += static_cast<double>(out[static_cast<size_t>(n)]) * out[static_cast<size_t>(n)];
        }
        inRms = std::sqrt(inRms / n2);
        outRms = std::sqrt(outRms / n2);
        auto ratio = outRms / inRms;
        std::printf("  dry RMS: %.4f, chorus=1 RMS: %.4f (ratio %.3f, expect close to 1.0)\n", inRms, outRms, ratio);
        bool stayslight = ratio > 0.8 && ratio < 1.2;
        std::printf("  %s: chorus stays light even at max\n\n", stayslight ? "OK" : "FAILED");
        allPassed &= stayslight;
    }

    // --- Test 5: stability over a long run at max settings. ---
    std::printf("=== Stability at max chorus over a sustained tone ===\n");
    {
        auto out = runChorus(1.0f, static_cast<int>(sampleRate * 3));
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
