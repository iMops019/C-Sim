// Step 2 of the amp-modeling build order: confirm 4x oversampling actually
// removes the aliasing a nonlinear stage introduces, before chaining
// anything further. Drives KorenTriodeStage hard with a single tone whose
// 5th harmonic exceeds Nyquist at 48kHz, then measures energy at the
// frequency that harmonic aliases down to - a frequency that is NOT a
// true harmonic of the fundamental, so any energy there can only be
// aliasing.

#include "../Source/dsp/KorenTriodeStage.h"
#include "../Source/dsp/Oversampler4x.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    // Goertzel algorithm: magnitude of a real signal at one target
    // frequency, without computing a full FFT.
    double goertzelMagnitude(const std::vector<float>& samples, double targetFreq, double sampleRate)
    {
        auto n = samples.size();
        auto k = 0.5 + (static_cast<double>(n) * targetFreq / sampleRate);
        auto w = (2.0 * M_PI / static_cast<double>(n)) * k;
        auto cosw = std::cos(w);
        auto coeff = 2.0 * cosw;
        double q0 = 0.0, q1 = 0.0, q2 = 0.0;

        for (auto s : samples)
        {
            q0 = coeff * q1 - q2 + static_cast<double>(s);
            q2 = q1;
            q1 = q0;
        }

        auto real = q1 - q2 * cosw;
        auto imag = q2 * std::sin(w);
        return std::sqrt(real * real + imag * imag) / (static_cast<double>(n) / 2.0);
    }

    double toDb(double linear)
    {
        return 20.0 * std::log10(std::max(linear, 1.0e-12));
    }
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr double fundamental = 5500.0;
    // 5th harmonic = 27500Hz, which exceeds 24000Hz Nyquist and folds back
    // to 48000-27500=20500Hz. 20500/5500 is not an integer, so this is not
    // a real harmonic of the fundamental - any energy here is aliasing.
    constexpr double aliasFreq = 20500.0;
    constexpr int numSamples = 8192;

    std::vector<float> drySignal(numSamples);
    for (int n = 0; n < numSamples; ++n)
        drySignal[static_cast<size_t>(n)] =
            static_cast<float>(0.9 * std::sin(2.0 * M_PI * fundamental * n / sampleRate));

    KorenTriodeStage::Parameters hotParams;
    hotParams.inputToGridVolts = 20.0; // hard drive - lots of harmonic content
    KorenTriodeStage stage(hotParams);

    std::vector<float> noOversampling(numSamples);
    for (int n = 0; n < numSamples; ++n)
        noOversampling[static_cast<size_t>(n)] = stage.processSample(drySignal[static_cast<size_t>(n)]);

    Oversampler4x oversampler(sampleRate);
    std::vector<float> withOversampling(numSamples);
    oversampler.processBlock(drySignal.data(), withOversampling.data(), numSamples,
                              [&stage](float x) { return stage.processSample(x); });

    auto fundamentalLevel = goertzelMagnitude(noOversampling, fundamental, sampleRate);
    auto aliasNoOS = goertzelMagnitude(noOversampling, aliasFreq, sampleRate);
    auto aliasWithOS = goertzelMagnitude(withOversampling, aliasFreq, sampleRate);

    std::printf("=== Aliasing test: %.0fHz driven hard through KorenTriodeStage ===\n", fundamental);
    std::printf("Checking energy at %.0fHz - not a true harmonic of %.0fHz (the 5th\n", aliasFreq, fundamental);
    std::printf("harmonic, 27500Hz, aliases here once it exceeds 24kHz Nyquist).\n\n");
    std::printf("  fundamental level:              %8.5f (%6.1f dB)\n", fundamentalLevel, toDb(fundamentalLevel));
    std::printf("  alias energy, no oversampling:  %8.5f (%6.1f dB)\n", aliasNoOS, toDb(aliasNoOS));
    std::printf("  alias energy, 4x oversampled:   %8.5f (%6.1f dB)\n", aliasWithOS, toDb(aliasWithOS));
    auto reductionDb = toDb(aliasNoOS) - toDb(aliasWithOS);
    std::printf("  reduction from oversampling:    %6.1f dB\n", reductionDb);

    bool pass = reductionDb > 15.0;
    std::printf("\n%s: oversampling %s reduce aliased energy by >15dB.\n",
                pass ? "OK" : "FAILED", pass ? "does" : "does NOT");

    return pass ? 0 : 1;
}
