// Step 5 of the amp-modeling build order: verify the power amp stage's
// three features in isolation, each against a clear before/after
// comparison rather than just "no crash":
//  1. Push-pull even-harmonic cancellation vs. a single-ended reference.
//  2. Sag - output should compress more once the rail has had time to
//     droop, vs. right at the start of a sustained note.
//  3. Negative feedback - higher feedback should mean lower net gain.

#include "../Source/dsp/KorenTriodeStage.h"
#include "../Source/dsp/PowerAmpStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 110.0; // A2

    double peakOf(const std::vector<float>& v, size_t from, size_t count)
    {
        double peak = 0.0;
        for (size_t i = from; i < from + count && i < v.size(); ++i)
            peak = std::max(peak, static_cast<double>(std::abs(v[i])));
        return peak;
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: push-pull even-harmonic cancellation ---
    std::printf("=== Push-pull even-harmonic cancellation ===\n");

    constexpr int numSamples = 8192;
    std::vector<float> input(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

    // Single-ended reference: same nonlinearity, no push-pull combination.
    KorenTriodeStage::Parameters powerParams;
    powerParams.plateVoltage = 300.0;
    powerParams.gridBias = -2.0;
    powerParams.inputToGridVolts = 4.0;
    KorenTriodeStage singleEnded(powerParams);

    std::vector<float> singleEndedOut(numSamples);
    for (int n = 0; n < numSamples; ++n)
        singleEndedOut[static_cast<size_t>(n)] = singleEnded.processSample(input[static_cast<size_t>(n)]);

    PowerAmpStage pushPull(sampleRate);
    pushPull.setSag(0.0f);      // isolate push-pull behaviour from sag/feedback
    pushPull.setFeedback(0.0f);
    std::vector<float> pushPullOut(numSamples);
    pushPull.processBlock(input.data(), pushPullOut.data(), numSamples);

    auto singleEnded2nd = TestUtils::goertzelMagnitude(singleEndedOut, testFreq * 2.0, sampleRate);
    auto singleEnded3rd = TestUtils::goertzelMagnitude(singleEndedOut, testFreq * 3.0, sampleRate);
    auto pushPull2nd = TestUtils::goertzelMagnitude(pushPullOut, testFreq * 2.0, sampleRate);
    auto pushPull3rd = TestUtils::goertzelMagnitude(pushPullOut, testFreq * 3.0, sampleRate);

    std::printf("  2nd harmonic: single-ended=%.5f  push-pull=%.5f  (push-pull should be much smaller)\n",
                singleEnded2nd, pushPull2nd);
    std::printf("  3rd harmonic: single-ended=%.5f  push-pull=%.5f  (push-pull should be close to single-ended)\n",
                singleEnded3rd, pushPull3rd);

    // The meaningful claim isn't "3rd > 2nd" in absolute terms - it's that
    // push-pull suppresses the EVEN harmonic much more than it suppresses
    // the ODD one, relative to what each looked like single-ended.
    bool evenCancelled = pushPull2nd < singleEnded2nd * 0.15;
    bool oddSurvives = pushPull3rd > singleEnded3rd * 0.5;
    std::printf("  %s: push-pull suppresses the 2nd harmonic by >~16dB vs single-ended\n",
                evenCancelled ? "OK" : "FAILED");
    std::printf("  %s: 3rd harmonic keeps at least half its single-ended strength\n\n",
                oddSurvives ? "OK" : "FAILED");
    allPassed &= evenCancelled && oddSurvives;

    // --- Test 2: sag - compression should grow over a sustained note ---
    std::printf("=== Sag: compression should grow over a sustained note ===\n");

    constexpr int sagTestSamples = 20000; // ~416ms at 48kHz, comfortably longer than the ~150ms release
    std::vector<float> loudInput(sagTestSamples);
    for (int n = 0; n < sagTestSamples; ++n)
        loudInput[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

    PowerAmpStage sagged(sampleRate);
    sagged.setSag(1.0f);
    sagged.setFeedback(0.0f);
    std::vector<float> saggedOut(sagTestSamples);
    sagged.processBlock(loudInput.data(), saggedOut.data(), sagTestSamples);

    PowerAmpStage unsagged(sampleRate);
    unsagged.setSag(0.0f);
    unsagged.setFeedback(0.0f);
    std::vector<float> unsaggedOut(sagTestSamples);
    unsagged.processBlock(loudInput.data(), unsaggedOut.data(), sagTestSamples);

    auto earlyWindow = static_cast<size_t>(0.02 * sampleRate);  // first ~20ms
    auto lateStart = static_cast<size_t>(0.3 * sampleRate);     // ~300ms in
    auto windowLen = static_cast<size_t>(0.02 * sampleRate);

    auto saggedEarlyPeak = peakOf(saggedOut, 0, earlyWindow);
    auto saggedLatePeak = peakOf(saggedOut, lateStart, windowLen);
    auto unsaggedEarlyPeak = peakOf(unsaggedOut, 0, earlyWindow);
    auto unsaggedLatePeak = peakOf(unsaggedOut, lateStart, windowLen);

    std::printf("  sag=1.0:  early peak=%.4f  late peak=%.4f  (late should be lower)\n", saggedEarlyPeak, saggedLatePeak);
    std::printf("  sag=0.0:  early peak=%.4f  late peak=%.4f  (should stay close)\n", unsaggedEarlyPeak, unsaggedLatePeak);

    bool sagCompresses = saggedLatePeak < saggedEarlyPeak * 0.9;
    bool noSagStable = std::abs(unsaggedLatePeak - unsaggedEarlyPeak) < unsaggedEarlyPeak * 0.1;
    std::printf("  %s: sag=1.0 measurably compresses the sustained note\n", sagCompresses ? "OK" : "FAILED");
    std::printf("  %s: sag=0.0 stays essentially level (no droop)\n\n", noSagStable ? "OK" : "FAILED");
    allPassed &= sagCompresses && noSagStable;

    // --- Test 3: negative feedback should reduce net gain ---
    std::printf("=== Negative feedback reduces net gain ===\n");

    PowerAmpStage noFeedback(sampleRate);
    noFeedback.setSag(0.0f);
    noFeedback.setFeedback(0.0f);
    std::vector<float> noFeedbackOut(numSamples);
    noFeedback.processBlock(input.data(), noFeedbackOut.data(), numSamples);

    PowerAmpStage highFeedback(sampleRate);
    highFeedback.setSag(0.0f);
    highFeedback.setFeedback(0.8f);
    std::vector<float> highFeedbackOut(numSamples);
    highFeedback.processBlock(input.data(), highFeedbackOut.data(), numSamples);

    auto gainNoFeedback = TestUtils::goertzelMagnitude(noFeedbackOut, testFreq, sampleRate);
    auto gainHighFeedback = TestUtils::goertzelMagnitude(highFeedbackOut, testFreq, sampleRate);

    std::printf("  fundamental, feedback=0.0: %.4f\n", gainNoFeedback);
    std::printf("  fundamental, feedback=0.8: %.4f  (should be lower)\n", gainHighFeedback);
    bool feedbackReducesGain = gainHighFeedback < gainNoFeedback;
    std::printf("  %s\n\n", feedbackReducesGain ? "OK: feedback reduces net gain" : "FAILED");
    allPassed &= feedbackReducesGain;

    // --- Test 4: stability across dropped-tuning range at extreme settings ---
    std::printf("=== Stability across dropped-tuning range, max sag + max feedback ===\n");
    bool stable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        PowerAmpStage extreme(sampleRate);
        extreme.setSag(1.0f);
        extreme.setFeedback(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.95 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        extreme.processBlock(sweepIn.data(), sweepOut.data(), 4096);

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
