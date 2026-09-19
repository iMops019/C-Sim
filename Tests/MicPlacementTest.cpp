// Verify MicPlacement against the physics it claims to model:
//  - the proximity shelf matches the analytic first-order response
//    H(s) = (s + a)/(s + a0), is exactly identity at the reference
//    distance, boosts bass up close and cuts it far away, boosts a ribbon
//    more than a dynamic, and never touches the high end;
//  - the Distance knob's mapping and the relative arrival time are right;
//  - the fractional delay is exact at whole-sample delays, interpolates
//    between them, and glides (monotonically, without blowing up) when its
//    target moves.

#include "../Source/dsp/MicPlacement.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    constexpr double sampleRate = 48000.0;

    // Steady-state gain of the shelf at one frequency, measured by feeding
    // it a sine (not read off the coefficients, so it tests the whole
    // filter as it actually runs).
    double measuredGainDb(float kappa, double distance, double freq)
    {
        MicPlacement::ProximityShelf shelf;
        shelf.prepare(sampleRate);
        shelf.setPlacement(kappa, distance, MicPlacement::referenceMetres());

        constexpr int n = 48000;
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            auto x = static_cast<float>(std::sin(2.0 * M_PI * freq * i / sampleRate));
            auto y = shelf.processSample(x);
            if (i >= n / 2) // settled
            {
                inSq += static_cast<double>(x) * x;
                outSq += static_cast<double>(y) * y;
            }
        }
        return 10.0 * std::log10(outSq / inSq);
    }

    // The textbook analytic response, straight from the formula.
    double analyticGainDb(float kappa, double distance, double freq)
    {
        auto w = 2.0 * M_PI * freq;
        auto a = static_cast<double>(kappa) * MicPlacement::speedOfSound / distance;
        auto a0 = static_cast<double>(kappa) * MicPlacement::speedOfSound / MicPlacement::referenceMetres();
        auto h = std::complex<double>(a, w) / std::complex<double>(a0, w);
        return 20.0 * std::log10(std::abs(h));
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    const auto close = MicPlacement::distanceMetres(0.0f);
    const auto far = MicPlacement::distanceMetres(100.0f);
    const auto reference = MicPlacement::referenceMetres();

    std::printf("=== Distance knob mapping ===\n");
    {
        std::printf("  Distance 0 = %.1f in, 50 = %.1f in, 100 = %.1f in\n",
                     close / 0.0254, MicPlacement::distanceMetres(50.0f) / 0.0254, far / 0.0254);
        check(std::abs(close - 0.0254) < 1.0e-9 && std::abs(far - 0.4572) < 1.0e-9, "sweeps 1 inch to 18 inches");
        check(std::abs(MicPlacement::distanceMetres(50.0f) - reference) < 1.0e-9,
              "the knob's midpoint IS the reference distance (default = no change)");

        bool monotonic = true;
        for (float k = 1.0f; k <= 100.0f; k += 1.0f)
            monotonic &= MicPlacement::distanceMetres(k) > MicPlacement::distanceMetres(k - 1.0f);
        check(monotonic, "further knob = further away, at every step");
    }

    std::printf("\n=== Arrival time ===\n");
    {
        auto maxDelay = MicPlacement::relativeDelaySeconds(far, close);
        std::printf("  1 in vs 18 in: %.3f ms (%.1f samples at 48kHz)\n", maxDelay * 1000.0, maxDelay * sampleRate);
        check(std::abs(maxDelay - (0.4572 - 0.0254) / 343.0) < 1.0e-9, "delay = extra distance / speed of sound");
        check(MicPlacement::relativeDelaySeconds(close, far) == 0.0, "the nearer mic is never delayed");
        check(MicPlacement::relativeDelaySeconds(reference, reference) == 0.0, "equal distances = no delay");
    }

    std::printf("\n=== Proximity shelf: identity at the reference distance ===\n");
    {
        MicPlacement::ProximityShelf shelf;
        shelf.prepare(sampleRate);
        shelf.setPlacement(MicPlacement::kappaDynamic, reference, reference);
        bool identical = true;
        for (int i = 0; i < 2000; ++i)
        {
            auto x = static_cast<float>(std::sin(0.05 * i) * 0.7 + std::sin(0.9 * i) * 0.2);
            identical &= std::abs(shelf.processSample(x) - x) < 1.0e-6f;
        }
        check(identical, "output equals input exactly at r = r0");
    }

    std::printf("\n=== Proximity shelf: matches the analytic response ===\n");
    for (auto kappa : { MicPlacement::kappaDynamic, MicPlacement::kappaRibbon })
    {
        double worst = 0.0;
        for (auto distance : { close, far })
            for (double f : { 60.0, 100.0, 200.0, 400.0, 1000.0, 2000.0 })
                worst = std::max(worst, std::abs(measuredGainDb(kappa, distance, f) - analyticGainDb(kappa, distance, f)));
        std::printf("  kappa %.1f: worst deviation from the formula %.2f dB (60Hz-2kHz, close and far)\n", kappa, worst);
        check(worst < 0.7, "digital filter follows H(s) = (s + a)/(s + a0)");
    }

    std::printf("\n=== Proximity shelf: bass boost close, cut far, highs untouched ===\n");
    {
        auto dynClose = measuredGainDb(MicPlacement::kappaDynamic, close, 100.0);
        auto dynFar = measuredGainDb(MicPlacement::kappaDynamic, far, 100.0);
        auto ribClose = measuredGainDb(MicPlacement::kappaRibbon, close, 100.0);
        std::printf("  100Hz: dynamic at 1 in %+.1f dB, dynamic at 18 in %+.1f dB, ribbon at 1 in %+.1f dB\n",
                     dynClose, dynFar, ribClose);
        check(dynClose > 8.0 && dynClose < 14.0, "a dynamic 1 inch from the cab boosts 100Hz by roughly 8-14 dB");
        check(dynFar < -4.0, "18 inches away, bass is down by more than 4 dB");
        check(ribClose > dynClose + 0.5, "a ribbon has a stronger proximity effect than a dynamic");

        auto hfClose = measuredGainDb(MicPlacement::kappaRibbon, close, 8000.0);
        auto hfFar = measuredGainDb(MicPlacement::kappaRibbon, far, 8000.0);
        std::printf("  8kHz: ribbon at 1 in %+.2f dB, at 18 in %+.2f dB\n", hfClose, hfFar);
        check(std::abs(hfClose) < 0.3 && std::abs(hfFar) < 0.3, "the high end is left alone");
    }

    std::printf("\n=== Fractional delay ===\n");
    {
        auto impulseResponse = [](float delay, int length)
        {
            MicPlacement::FractionalDelay d;
            d.prepare(sampleRate, 0.005);
            d.setDelaySamples(delay, true);
            std::vector<float> out(static_cast<size_t>(length));
            for (int i = 0; i < length; ++i)
                out[static_cast<size_t>(i)] = d.processSample(i == 0 ? 1.0f : 0.0f);
            return out;
        };

        auto whole = impulseResponse(10.0f, 40);
        bool exact = std::abs(whole[10] - 1.0f) < 1.0e-6f;
        for (size_t i = 0; i < whole.size(); ++i)
            if (i != 10 && std::abs(whole[i]) > 1.0e-6f)
                exact = false;
        check(exact, "a 10-sample delay moves an impulse to exactly sample 10");

        auto half = impulseResponse(10.5f, 40);
        check(std::abs(half[10] - 0.5f) < 1.0e-6f && std::abs(half[11] - 0.5f) < 1.0e-6f,
              "a 10.5-sample delay splits an impulse evenly across samples 10 and 11");

        auto zero = impulseResponse(0.0f, 8);
        check(std::abs(zero[0] - 1.0f) < 1.0e-6f, "zero delay passes the signal straight through");

        // Glide: target jumps 0 -> 40; the delay must rise smoothly, never overshoot.
        MicPlacement::FractionalDelay d;
        d.prepare(sampleRate, 0.005);
        d.setDelaySamples(0.0f, true);
        d.setDelaySamples(40.0f);
        float previous = 0.0f;
        bool monotonic = true, bounded = true, finite = true;
        for (int i = 0; i < 6000; ++i)
        {
            auto y = d.processSample(static_cast<float>(std::sin(0.1 * i)));
            finite &= std::isfinite(y);
            auto now = d.getCurrentDelaySamples();
            monotonic &= now >= previous;
            bounded &= now <= 40.0f + 1.0e-3f;
            previous = now;
        }
        std::printf("  after 6000 samples (0.125 s) the delay has glided to %.2f of 40 samples\n", previous);
        check(monotonic && bounded && finite, "the delay glides up smoothly, never overshoots, output stays finite");
        check(previous > 39.9f, "and arrives at its target");

        // Asking for more delay than the buffer holds must clamp, not crash.
        MicPlacement::FractionalDelay small;
        small.prepare(sampleRate, 0.001);
        small.setDelaySamples(1.0e6f, true);
        bool safe = true;
        for (int i = 0; i < 1000; ++i)
            safe &= std::isfinite(small.processSample(0.5f));
        check(safe, "an absurd delay request is clamped to the buffer, not read out of bounds");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
