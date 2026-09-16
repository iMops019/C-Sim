// Step 4 of the amp-modeling build order: chain gain stages and test on
// the metal build's actual target signal - a drop-tuned low string, not
// an open chord ("that's the make-or-break signal" per the build order).
// C2 (~65.4Hz) is the low string in drop C.

#include "../Source/dsp/MetalPreampChain.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double dropCLowString = 65.41;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: sustained low-string tone - fundamental survival,
    // harmonic generation, and rumble rejection. ---
    std::printf("=== Sustained %.2fHz tone (drop C low string), drive=0.8 ===\n", dropCLowString);

    MetalPreampChain chain(sampleRate);
    chain.setDrive(0.8f);

    constexpr int numSamples = 8192;
    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));

    chain.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, dropCLowString, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 3.0, sampleRate);
    auto rumble = TestUtils::goertzelMagnitude(output, 40.0, sampleRate);

    std::printf("  fundamental (%.2fHz):        %.5f\n", dropCLowString, fundamental);
    std::printf("  2nd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 3.0, thirdHarmonic);
    std::printf("  40Hz rumble (below the note): %.5f  (should be well below fundamental)\n", rumble);

    bool fundamentalSurvives = fundamental > 0.05; // the note itself must still be clearly present
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.01; // real harmonic content being generated
    bool rumbleTight = rumble < fundamental * 0.3; // pre-gain HPF doing its job

    std::printf("  %s: fundamental survives the cascade\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: cascade is generating real harmonic distortion\n", distorting ? "OK" : "FAILED");
    std::printf("  %s: sub-fundamental rumble is tightened relative to the note\n\n", rumbleTight ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting && rumbleTight;

    // --- Test 2: percussive chug burst - fast-decaying note simulating a
    // palm mute, checking for stability across attack, decay, and silence. ---
    std::printf("=== Palm-muted chug burst (fast decay) ===\n");

    MetalPreampChain chugChain(sampleRate);
    chugChain.setDrive(0.8f);

    constexpr int burstSamples = 16000; // includes a stretch of near-silence at the end
    std::vector<float> chugIn(burstSamples), chugOut(burstSamples);
    for (int n = 0; n < burstSamples; ++n)
    {
        auto t = static_cast<double>(n) / sampleRate;
        auto envelope = std::exp(-t / 0.08); // ~80ms decay, typical palm-mute tightness
        chugIn[static_cast<size_t>(n)] = static_cast<float>(0.8 * envelope * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));
    }

    chugChain.processBlock(chugIn.data(), chugOut.data(), burstSamples);

    bool sawNonFinite = false;
    double peakOut = 0.0;
    for (auto y : chugOut)
    {
        if (! std::isfinite(y)) sawNonFinite = true;
        peakOut = std::max(peakOut, static_cast<double>(std::abs(y)));
    }

    std::printf("  peak output: %.4f   non-finite samples: %s\n", peakOut, sawNonFinite ? "YES <-- FAIL" : "no");
    allPassed &= ! sawNonFinite;

    // A well-gain-staged preamp shouldn't blow input amplitude up by an
    // absurd multiple - the loudness should come from saturation/harmonic
    // content, not raw peak scaling. 0.8 input peak -> flag anything
    // wildly outside a "hot but sane" 0.3x-4x range.
    constexpr double inputPeak = 0.8;
    auto peakRatio = peakOut / inputPeak;
    bool levelSane = peakRatio > 0.3 && peakRatio < 4.0;
    std::printf("  peak ratio vs input: %.2fx  %s\n", peakRatio,
                levelSane ? "OK (sane gain staging)" : "FAILED (absurdly hot or dead quiet)");
    allPassed &= levelSane;

    // --- Test 3: stability sweep across common dropped-tuning low strings
    // (roughly E1 to E2) at maximum drive. ---
    std::printf("\n=== Stability across dropped-tuning range, max drive ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        MetalPreampChain sweepChain(sampleRate);
        sweepChain.setDrive(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweepChain.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz, max drive\n", freq);
                rangeStable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole dropped-tuning range" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
