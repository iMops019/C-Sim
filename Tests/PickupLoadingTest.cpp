// Verify PickupLoading - the correction that makes a recorded guitar sound the
// way it would into a lower-impedance amp input (the Marshall 1959's Low jack).
//
//  1. Against numbers computed independently. The lumped pickup model was
//     researched separately (Lemme, Zollner, Ban) and its predictions for the
//     Marshall Low jack (136k) written down BEFORE this filter existed:
//        single coil  -1.0 / -2.2 / -3.7 dB at 2 / 3 / 5 kHz,
//        humbucker    -2.9 / -6.2 / -2.0 dB,
//     and for a 500k High jack -0.1/-0.4/-0.7 and -0.5/-1.3/-0.3. The filter
//     must land within 0.4 dB of them.
//  2. Against the physics it is built from: the pickup's own response into each
//     load (evaluated directly, 1/P(s)) has its resonance lower AND lower in
//     frequency for the lower load - and the correction is exactly the ratio.
//  3. The digital filter equals its analog target (magnitude and phase) from
//     100Hz to 15kHz at the rate the amp runs it (192kHz).
//  4. It only ever damps: gain never above 0dB, the high-frequency gain is 1,
//     and the more the load falls the more it takes off; a load at or above the
//     reference does NOTHING (it refuses to boost).
//  5. It is stable on noise and reset() clears it.

#include "../Source/dsp/PickupLoading.h"

#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using Cx = std::complex<double>;
    constexpr double fs = 192000.0;

    Cx tone(const std::vector<double>& x, double f)
    {
        Cx sum = 0.0;
        auto n = static_cast<double>(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1.0));
            sum += w * x[i] * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(i) / fs);
        }
        return sum;
    }

    Cx measured(const PickupLoading::Setup& setup, double f)
    {
        PickupLoading p;
        p.prepare(fs);
        p.configure(setup);
        auto settle = static_cast<int>(0.05 * fs), n = static_cast<int>(std::max(30.0 * fs / f, 0.05 * fs));
        std::vector<double> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = -settle; i < n; ++i)
        {
            auto x = std::sin(2.0 * M_PI * f * (i + settle) / fs);
            auto y = p.process(x);
            if (i >= 0) { in[static_cast<size_t>(i)] = x; out[static_cast<size_t>(i)] = y; }
        }
        return tone(out, f) / tone(in, f);
    }

    double db(Cx h) { return 20.0 * std::log10(std::max(std::abs(h), 1.0e-12)); }

    // The pickup's OWN response into a load: 1 / P(s), evaluated directly.
    Cx pickupInto(const PickupLoading::Setup& setup, double loadOhms, double f)
    {
        auto ct = setup.pickup.farads + setup.cableFarads;
        Cx s(0.0, 2.0 * M_PI * f);
        return 1.0 / (s * s * setup.pickup.henries * ct + s * (setup.pickup.ohms * ct + setup.pickup.henries / loadOhms) + (1.0 + setup.pickup.ohms / loadOhms));
    }

    double parallel(double a, double b) { return a * b / (a + b); }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    struct Expect { const char* name; PickupLoading::Pickup pickup; double jack; double d2, d3, d5; };
    const Expect expectations[] = {
        { "single coil, Low jack 136k", PickupLoading::singleCoil(), 136.0e3, -1.0, -2.2, -3.7 },
        { "humbucker,   Low jack 136k", PickupLoading::humbucker(), 136.0e3, -2.9, -6.2, -2.0 },
        { "single coil, High jack 500k", PickupLoading::singleCoil(), 500.0e3, -0.1, -0.4, -0.7 },
        { "humbucker,   High jack 500k", PickupLoading::humbucker(), 500.0e3, -0.5, -1.3, -0.3 },
    };

    std::printf("=== Against the independently computed figures (dB at 2 / 3 / 5 kHz) ===\n");
    for (auto& e : expectations)
    {
        auto setup = PickupLoading::setupFor(e.pickup, e.jack);
        auto a = db(PickupLoading::response(setup, 2000.0)), b = db(PickupLoading::response(setup, 3000.0)), c = db(PickupLoading::response(setup, 5000.0));
        std::printf("  %-28s %+.1f / %+.1f / %+.1f   (expected %+.1f / %+.1f / %+.1f)\n", e.name, a, b, c, e.d2, e.d3, e.d5);
        check(std::abs(a - e.d2) < 0.4 && std::abs(b - e.d3) < 0.4 && std::abs(c - e.d5) < 0.4, "within 0.4dB of the researched figures");
    }
    std::printf("\n");

    std::printf("=== The pickup's resonance into each load (the physics the correction is a ratio of) ===\n");
    for (auto pickup : { PickupLoading::singleCoil(), PickupLoading::humbucker() })
    {
        auto setup = PickupLoading::setupFor(pickup, 136.0e3);
        auto rRef = parallel(setup.potsOhms, setup.referenceInputOhms), rAmp = parallel(setup.potsOhms, setup.ampInputOhms);
        double peakRef = -1e9, peakAmp = -1e9, atRef = 0.0, atAmp = 0.0;
        for (double f = 500.0; f < 12000.0; f *= 1.01)
        {
            auto r = db(pickupInto(setup, rRef, f)), a = db(pickupInto(setup, rAmp, f));
            if (r > peakRef) { peakRef = r; atRef = f; }
            if (a > peakAmp) { peakAmp = a; atAmp = f; }
        }
        std::printf("  %s: resonance %+.1f dB at %.0f Hz into the reference load, %+.1f dB at %.0f Hz into the Low jack\n",
                     pickup.henries > 3.0 ? "humbucker  " : "single coil", peakRef, atRef, peakAmp, atAmp);
        check(peakAmp < peakRef - 1.5, "a lower load takes the resonance's height down (>1.5dB)");
        check(atAmp < atRef, "and moves it lower in frequency");

        // The correction is exactly the ratio of the two direct responses.
        double worst = 0.0;
        for (double f : { 300.0, 1000.0, 2500.0, 4000.0, 8000.0 })
            worst = std::max(worst, std::abs(db(pickupInto(setup, rAmp, f) / pickupInto(setup, rRef, f)) - db(PickupLoading::response(setup, f))));
        check(worst < 1.0e-9, "response() is exactly H(amp load) / H(reference load)");
    }
    std::printf("\n");

    std::printf("=== The digital filter vs its analog target (192kHz) ===\n");
    {
        double worstDb = 0.0, worstDeg = 0.0;
        for (auto pickup : { PickupLoading::singleCoil(), PickupLoading::humbucker() })
        {
            auto setup = PickupLoading::setupFor(pickup, 136.0e3);
            for (double f : { 100.0, 500.0, 1500.0, 2500.0, 3500.0, 5000.0, 8000.0, 15000.0 })
            {
                auto m = measured(setup, f), a = PickupLoading::response(setup, f);
                worstDb = std::max(worstDb, std::abs(db(m) - db(a)));
                worstDeg = std::max(worstDeg, std::abs(std::arg(m / a)) * 180.0 / M_PI);
            }
        }
        std::printf("  worst difference: %.4f dB, %.3f deg\n", worstDb, worstDeg);
        check(worstDb < 0.05 && worstDeg < 0.5, "digital filter matches the analog response to 0.05dB and 0.5 degrees");
    }
    std::printf("\n");

    std::printf("=== It only damps ===\n");
    {
        auto setup = PickupLoading::setupFor(PickupLoading::humbucker(), 136.0e3);
        double most = -1e9;
        for (double f = 20.0; f < 40000.0; f *= 1.05)
            most = std::max(most, db(PickupLoading::response(setup, f)));
        std::printf("  the largest gain anywhere from 20Hz to 40kHz: %+.3f dB\n", most);
        check(most < 0.0, "never above 0dB (a damping filter, never a boost)");
        check(std::abs(db(measured(setup, 40000.0))) < 0.5, "the high-frequency gain is 1");

        double prev = 1.0e9;
        bool monotonic = true;
        for (double jack : { 1.0e6, 500.0e3, 250.0e3, 136.0e3, 68.0e3 })
        {
            auto g = db(PickupLoading::response(PickupLoading::setupFor(PickupLoading::humbucker(), jack), 3000.0));
            monotonic &= g <= prev + 1.0e-9;
            prev = g;
        }
        check(monotonic, "the lower the input impedance, the more it takes off at 3kHz");

        PickupLoading p;
        p.prepare(fs);
        p.configure(PickupLoading::setupFor(PickupLoading::humbucker(), 2.0e6));
        auto exact = true;
        for (int i = 0; i < 1000; ++i) exact &= p.process(std::sin(0.01 * i)) == std::sin(0.01 * i);
        check(exact, "a load ABOVE the reference is refused: the signal passes untouched");
        p.configure(PickupLoading::setupFor(PickupLoading::humbucker(), 1.0e6));
        auto identity = true;
        for (int i = 0; i < 1000; ++i) identity &= p.process(std::sin(0.02 * i)) == std::sin(0.02 * i);
        check(identity, "and a load equal to the reference does nothing");
    }
    std::printf("\n");

    std::printf("=== Stability ===\n");
    {
        PickupLoading p;
        p.prepare(fs);
        p.configure(PickupLoading::setupFor(PickupLoading::humbucker(), 68.0e3));
        unsigned seed = 5u;
        bool ok = true;
        double peak = 0.0;
        for (int i = 0; i < 400000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            auto y = p.process(static_cast<double>(seed >> 8) / 8388608.0 - 1.0);
            ok &= std::isfinite(y);
            peak = std::max(peak, std::abs(y));
        }
        std::printf("  peak %.3f for full-scale noise in\n", peak);
        check(ok && peak < 1.5, "finite and bounded on full-scale noise (a damping filter cannot add gain)");

        p.reset();
        auto first = p.process(1.0);
        p.reset();
        check(p.process(1.0) == first, "reset() clears its memory");
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
