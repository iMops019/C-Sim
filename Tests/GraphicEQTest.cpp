// Verify the graphic EQ actually does what a graphic EQ needs to: flat
// (all bands at 0dB) is a true bypass, boosting/cutting a band changes
// the level at that band's own frequency in the right direction, a
// boost stays mostly local (doesn't significantly move a distant band),
// and it stays stable at extreme settings.

#include "../Source/dsp/GraphicEQ.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 16384;

    std::vector<float> sineAt(double freq, float amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }

    double magnitudeAt(GraphicEQ& eq, double freq, float amplitude)
    {
        auto input = sineAt(freq, amplitude);
        std::vector<float> output(input.size());
        eq.processBlock(input.data(), output.data(), numSamples);
        return TestUtils::goertzelMagnitude(output, freq, sampleRate);
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: flat EQ is a true bypass ---
    // Compared against the dry signal's own Goertzel reading (not the raw
    // amplitude) since these band frequencies aren't exact FFT bins for
    // this block length - a rectangular-windowed single-frequency
    // correlation like this one has scalloping loss off-bin, which would
    // otherwise show up as a spurious several-dB "cut" at every band even
    // though the filter is mathematically exact unity gain at 0dB.
    std::printf("=== Flat (all bands 0dB): should pass a sine through essentially unchanged ===\n");
    {
        GraphicEQ eq(sampleRate);
        auto freqs = GraphicEQ::getBandFrequencies();
        bool allFlat = true;
        for (auto f : freqs)
        {
            auto dryMag = TestUtils::goertzelMagnitude(sineAt(f, 0.5f), f, sampleRate);
            auto wetMag = magnitudeAt(eq, f, 0.5f);
            auto diffDb = std::abs(TestUtils::toDb(wetMag) - TestUtils::toDb(dryMag));
            std::printf("  %.0fHz: dry=%.4f flat-EQ=%.4f, diff=%.4fdB\n", f, dryMag, wetMag, diffDb);
            if (diffDb > 0.05) allFlat = false;
        }
        std::printf("  %s: flat EQ passes every band frequency through at unity\n\n", allFlat ? "OK" : "FAILED");
        allPassed &= allFlat;
    }

    // --- Test 2: boosting/cutting a band moves that frequency the right way ---
    std::printf("=== Boost/cut: +12dB and -12dB on a band should raise/lower its own frequency ===\n");
    {
        auto freqs = GraphicEQ::getBandFrequencies();
        bool allCorrect = true;
        for (int band = 0; band < GraphicEQ::numBands; ++band)
        {
            auto f = freqs[static_cast<size_t>(band)];

            GraphicEQ flat(sampleRate);
            auto flatMag = magnitudeAt(flat, f, 0.2f);

            GraphicEQ boosted(sampleRate);
            boosted.setBandGainDb(band, 12.0f);
            auto boostedMag = magnitudeAt(boosted, f, 0.2f);

            GraphicEQ cut(sampleRate);
            cut.setBandGainDb(band, -12.0f);
            auto cutMag = magnitudeAt(cut, f, 0.2f);

            auto boostDb = TestUtils::toDb(boostedMag) - TestUtils::toDb(flatMag);
            auto cutDb = TestUtils::toDb(cutMag) - TestUtils::toDb(flatMag);

            std::printf("  band %d (%.0fHz): boost=%+.2fdB cut=%+.2fdB\n", band, f, boostDb, cutDb);

            // Should land close to the requested +/-12dB at the band's own
            // center - some tolerance for the shared-Q filter shape.
            if (boostDb < 8.0 || cutDb > -8.0)
                allCorrect = false;
        }
        std::printf("  %s: every band's boost/cut is felt at its own frequency\n\n", allCorrect ? "OK" : "FAILED");
        allPassed &= allCorrect;
    }

    // --- Test 3: a boost stays mostly local, not a broadband tilt ---
    std::printf("=== Locality: boosting the 100Hz band shouldn't significantly move 10kHz ===\n");
    {
        auto freqs = GraphicEQ::getBandFrequencies();
        auto lowFreq = freqs.front();
        auto highFreq = freqs.back();

        GraphicEQ flat(sampleRate);
        auto flatHighMag = magnitudeAt(flat, highFreq, 0.2f);

        GraphicEQ boostedLow(sampleRate);
        boostedLow.setBandGainDb(0, 12.0f);
        auto boostedHighMag = magnitudeAt(boostedLow, highFreq, 0.2f);

        auto spillDb = TestUtils::toDb(boostedHighMag) - TestUtils::toDb(flatHighMag);
        std::printf("  boosting %.0fHz by 12dB moves %.0fHz by %+.2fdB (should be small)\n", lowFreq, highFreq, spillDb);

        bool stayedLocal = std::abs(spillDb) < 1.0;
        std::printf("  %s\n\n", stayedLocal ? "OK: boost stayed local" : "FAILED");
        allPassed &= stayedLocal;
    }

    // --- Test 4: stability with every band maxed out ---
    std::printf("=== Stability: all bands at +15dB over a sustained chord-like signal ===\n");
    {
        GraphicEQ eq(sampleRate);
        for (int band = 0; band < GraphicEQ::numBands; ++band)
            eq.setBandGainDb(band, 15.0f);

        constexpr int longRun = 48000 * 2;
        std::vector<float> in(static_cast<size_t>(longRun)), out(static_cast<size_t>(longRun));
        for (int n = 0; n < longRun; ++n)
        {
            double t = static_cast<double>(n) / sampleRate;
            in[static_cast<size_t>(n)] = static_cast<float>(
                0.3 * std::sin(2.0 * M_PI * 110.0 * t) + 0.2 * std::sin(2.0 * M_PI * 440.0 * t) +
                0.15 * std::sin(2.0 * M_PI * 3000.0 * t));
        }
        eq.processBlock(in.data(), out.data(), longRun);

        bool sawNonFinite = false;
        double peak = 0.0;
        for (auto y : out)
        {
            if (!std::isfinite(y)) sawNonFinite = true;
            peak = std::max(peak, static_cast<double>(std::abs(y)));
        }
        std::printf("  peak output: %.4f, non-finite: %s\n", peak, sawNonFinite ? "YES <-- FAIL" : "no");

        bool stable = !sawNonFinite && peak < 20.0;
        std::printf("  %s\n\n", stable ? "OK: stable, bounded output" : "FAILED");
        allPassed &= stable;
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
