// Verify the Fortin Meshuggah preamp model's actual claims: real
// harmonic saturation on a dropped-tuning note, the Master control's
// diode stage adding real distortion on top of the tube cascade, a
// harder/more-clamped clipping character than MesaRectifierPreamp's
// pure-tube cascade at comparable settings (the actual point of giving
// these two preamps different clipping topologies), and stability.

#include "../Source/dsp/FortinMeshuggahPreamp.h"
#include "../Source/dsp/MesaRectifierPreamp.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double dropCLowString = 65.41;
    constexpr int numSamples = 8192;

    // Peak/RMS crest factor over the tail of a block (skips the
    // attack transient) - a harder-clipped, more "squared-off" waveform
    // has a lower crest factor than a softly-saturating one at the same
    // drive.
    double crestFactor(const std::vector<float>& samples)
    {
        auto start = samples.size() / 4; // skip the first quarter as settle-in
        double peak = 0.0, sumSquares = 0.0;
        size_t count = 0;
        for (auto i = start; i < samples.size(); ++i)
        {
            auto v = static_cast<double>(samples[i]);
            peak = std::max(peak, std::abs(v));
            sumSquares += v * v;
            ++count;
        }
        auto rms = std::sqrt(sumSquares / static_cast<double>(count));
        return rms > 1.0e-9 ? peak / rms : 0.0;
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: sustained low-string tone - fundamental survival,
    // harmonic generation, and rumble rejection. ---
    std::printf("=== Sustained %.2fHz tone (drop C low string), Gain1=0.7 Gain2=0.7 Master=0.5 ===\n", dropCLowString);

    FortinMeshuggahPreamp preamp(sampleRate);
    preamp.setGain1(0.7f);
    preamp.setGain2(0.7f);
    preamp.setMaster(0.5f);

    std::vector<float> input(numSamples), output(numSamples);
    for (int n = 0; n < numSamples; ++n)
        input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * dropCLowString * n / sampleRate));

    preamp.processBlock(input.data(), output.data(), numSamples);

    auto fundamental = TestUtils::goertzelMagnitude(output, dropCLowString, sampleRate);
    auto secondHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 2.0, sampleRate);
    auto thirdHarmonic = TestUtils::goertzelMagnitude(output, dropCLowString * 3.0, sampleRate);
    auto rumble = TestUtils::goertzelMagnitude(output, 40.0, sampleRate);

    std::printf("  fundamental (%.2fHz):        %.5f\n", dropCLowString, fundamental);
    std::printf("  2nd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 2.0, secondHarmonic);
    std::printf("  3rd harmonic (%.2fHz):       %.5f  (evidence of distortion)\n", dropCLowString * 3.0, thirdHarmonic);
    std::printf("  40Hz rumble (below the note): %.5f  (should be well below fundamental)\n", rumble);

    bool fundamentalSurvives = fundamental > 0.05;
    bool distorting = (secondHarmonic + thirdHarmonic) > 0.01;
    bool rumbleTight = rumble < fundamental * 0.3;

    std::printf("  %s: fundamental survives the cascade\n", fundamentalSurvives ? "OK" : "FAILED");
    std::printf("  %s: cascade is generating real harmonic distortion\n", distorting ? "OK" : "FAILED");
    std::printf("  %s: sub-fundamental rumble is tightened relative to the note\n\n", rumbleTight ? "OK" : "FAILED");
    allPassed &= fundamentalSurvives && distorting && rumbleTight;

    // --- Test 2: Master should squash the waveform further (lower crest
    // factor) on top of whatever Gain1/Gain2 already produce. A single
    // harmonic's ratio to the fundamental isn't a reliable proxy once
    // the tube cascade alone is already saturating hard (there's nowhere
    // for that one harmonic to grow even as the diode clips harder) -
    // crest factor keeps measuring real, growing compression even past
    // that point. ---
    std::printf("=== Master: higher settings should squash the waveform further (lower crest factor) ===\n");
    {
        constexpr double testFreq = 220.0;

        auto crestAt = [testFreq](float masterAmount)
        {
            // Gain1/Gain2 left at 0 (a straight 1x pass into each tube
            // stage) with a quiet input - the tube cascade's own fixed
            // interstage makeup gains already push a hotter signal into
            // near-full diode saturation regardless of Master, leaving no
            // room to see Master's own contribution (this is what earlier
            // attempts at this test measured).
            FortinMeshuggahPreamp p(sampleRate);
            p.setGain1(0.0f);
            p.setGain2(0.0f);
            p.setMaster(masterAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

            p.processBlock(in.data(), out.data(), numSamples);
            return crestFactor(out);
        };

        auto low = crestAt(0.0f);
        auto high = crestAt(1.0f);

        std::printf("  Master=0.0: crest factor = %.3f\n", low);
        std::printf("  Master=1.0: crest factor = %.3f (should be lower)\n", high);

        bool squashesFurther = high < low * 0.9;
        std::printf("  %s: Master audibly squashes the waveform beyond the tube cascade alone\n\n",
                     squashesFurther ? "OK" : "FAILED");
        allPassed &= squashesFurther;
    }

    // --- Test 3: at comparably hot settings, the diode master should
    // give Fortin a harder, more "squared-off" clip than Mesa's
    // pure-tube cascade - a real, audible topology difference, not two
    // preamps converging on the same sound. ---
    std::printf("=== Fortin vs. Mesa: diode master should clip harder than pure tube cascade ===\n");
    {
        constexpr double testFreq = 110.0;
        std::vector<float> hotInput(numSamples);
        for (int n = 0; n < numSamples; ++n)
            hotInput[static_cast<size_t>(n)] = static_cast<float>(0.7 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        FortinMeshuggahPreamp fortin(sampleRate);
        fortin.setGain1(1.0f);
        fortin.setGain2(1.0f);
        fortin.setMaster(1.0f);
        std::vector<float> fortinOut(numSamples);
        fortin.processBlock(hotInput.data(), fortinOut.data(), numSamples);

        MesaRectifierPreamp mesa(sampleRate);
        mesa.setGain(1.0f);
        std::vector<float> mesaOut(numSamples);
        mesa.processBlock(hotInput.data(), mesaOut.data(), numSamples);

        auto fortinCrest = crestFactor(fortinOut);
        auto mesaCrest = crestFactor(mesaOut);

        std::printf("  Fortin crest factor: %.3f\n", fortinCrest);
        std::printf("  Mesa crest factor:   %.3f (should be higher - softer, less squared-off)\n", mesaCrest);

        bool fortinHarder = fortinCrest < mesaCrest;
        std::printf("  %s: Fortin's diode master clips harder than Mesa's pure tube cascade\n\n",
                     fortinHarder ? "OK" : "FAILED");
        allPassed &= fortinHarder;
    }

    // --- Test 4: stability sweep across common dropped-tuning low
    // strings (roughly E1 to E2) at maximum settings. ---
    std::printf("=== Stability across dropped-tuning range, max Gain1/Gain2/Master ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        FortinMeshuggahPreamp sweepPreamp(sampleRate);
        sweepPreamp.setGain1(1.0f);
        sweepPreamp.setGain2(1.0f);
        sweepPreamp.setMaster(1.0f);

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
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole dropped-tuning range" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
