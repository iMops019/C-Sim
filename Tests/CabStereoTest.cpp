// End-to-end check of the Cabinet's stereo behavior through the real pedal.
//
// Background (measured, and the reason this test exists): the synthetic
// mic IR used to render its right channel with a 0.3 ms delay "for stereo
// image". That was the same waveform 13 samples apart, not width - it
// put a -25 dB notch at ~1.7 kHz in the mono sum, made Width's mono end
// (Width = 0) sound comb-filtered, and pulled the image left. The mic is
// now a centered, mono-safe source, and all of the width comes from the
// room mic pair, so:
//   1. with no room, the output is perfectly centered and mono-safe;
//   2. more Room = more side energy (the room IS where width comes from);
//   3. Width scales that side energy exactly as advertised (0 = mono,
//      100 = as-is, 200 = +6 dB of side);
//   4. at the default Room the mono sum stays well-behaved.

#include "CabPedalTestKit.h"

#include <complex>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace CabTest;

namespace
{
    double levelDb(const std::vector<float>& x, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-12));
    }

    double energy(const std::vector<float>& x)
    {
        double e = 0.0;
        for (auto v : x) e += static_cast<double>(v) * v;
        return e;
    }

    std::vector<float> midOf(const Rendered& r)
    {
        std::vector<float> m(r.left.size());
        for (size_t n = 0; n < m.size(); ++n) m[n] = 0.5f * (r.left[n] + r.right[n]);
        return m;
    }

    std::vector<float> sideOf(const Rendered& r)
    {
        std::vector<float> s(r.left.size());
        for (size_t n = 0; n < s.size(); ++n) s[n] = 0.5f * (r.left[n] - r.right[n]);
        return s;
    }

    double sideToMidDb(const Rendered& r)
    {
        return 10.0 * std::log10(std::max(energy(sideOf(r)), 1.0e-30) / std::max(energy(midOf(r)), 1.0e-30));
    }

    // Deepest dip of the mono sum below the left channel's own spectrum.
    double worstMonoDipDb(const Rendered& r)
    {
        auto mid = midOf(r);
        double worst = 0.0;
        for (double f = 300.0; f <= 5000.0; f *= 1.03)
            worst = std::min(worst, levelDb(mid, f) - levelDb(r.left, f));
        return worst;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // A stock cab, Mic A only (so Mic B's different position isn't in the way).
    CabinetPedal pedal;
    prepared(pedal);
    param(pedal, "Mic Blend")->set(0.0f);
    for (int i = 0; i < 4; ++i)
    {
        render(pedal);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the synthetic IRs swap in
    }

    std::printf("=== No room: centered and mono-safe ===\n");
    {
        param(pedal, "Room")->set(0.0f);
        auto r = render(pedal);

        double maxDiff = 0.0;
        for (size_t n = 0; n < r.left.size(); ++n)
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(r.left[n] - r.right[n])));
        std::printf("  largest left/right sample difference: %.2g\n", maxDiff);
        check(maxDiff < 1.0e-6, "left and right are the same signal");
        check(peakIndex(r.left) == peakIndex(r.right), "no time offset between the channels (nothing pulls the image sideways)");
        check(worstMonoDipDb(r) > -0.01, "summing to mono changes nothing (was a -25 dB notch at ~1.7 kHz)");
    }

    std::printf("\n=== The room is where the width comes from ===\n");
    {
        double sideMid[4];
        int i = 0;
        for (float room : { 15.0f, 50.0f, 100.0f })
        {
            param(pedal, "Room")->set(room);
            sideMid[i] = sideToMidDb(render(pedal));
            ++i;
        }
        std::printf("  side/mid: Room 15%% %+.1f dB, 50%% %+.1f dB, 100%% %+.1f dB\n", sideMid[0], sideMid[1], sideMid[2]);
        check(sideMid[0] < sideMid[1] && sideMid[1] < sideMid[2], "more Room = more side energy");
        check(sideMid[2] > -3.0, "an all-room signal is close to fully decorrelated (side ~ mid)");
    }

    std::printf("\n=== Width scales the side signal as advertised ===\n");
    {
        param(pedal, "Room")->set(50.0f);
        double side[3];
        int i = 0;
        for (float w : { 50.0f, 100.0f, 200.0f })
        {
            param(pedal, "Width")->set(w);
            side[i++] = 10.0 * std::log10(energy(sideOf(render(pedal))));
        }
        std::printf("  side level vs Width 100: Width 50 %+.2f dB, Width 200 %+.2f dB (expect -6.02 / +6.02)\n",
                     side[0] - side[1], side[2] - side[1]);
        check(std::abs((side[0] - side[1]) + 6.02) < 0.3 && std::abs((side[2] - side[1]) - 6.02) < 0.3,
              "Width 50 halves the side signal, Width 200 doubles it");

        param(pedal, "Width")->set(0.0f);
        auto mono = render(pedal);
        double maxDiff = 0.0;
        for (size_t n = 0; n < mono.left.size(); ++n)
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(mono.left[n] - mono.right[n])));
        check(maxDiff < 1.0e-6, "Width 0 is exactly mono");
    }

    std::printf("\n=== The mono sum at the default Room is still well-behaved ===\n");
    {
        param(pedal, "Width")->set(100.0f);
        param(pedal, "Room")->set(15.0f);
        auto r = render(pedal);
        auto dip = worstMonoDipDb(r);
        std::printf("  Room 15%%: side/mid %+.1f dB, worst mono-sum dip %.1f dB\n", sideToMidDb(r), dip);
        check(dip > -12.0, "no deep notch when a default-Room signal is summed to mono");
    }

    std::printf("\n=== Mic Spread: a real stereo pair from the two mics ===\n");
    {
        // Two reference pedals: Mic A alone, and Mic B alone. Everything else identical.
        auto reference = [&](float blend)
        {
            auto p = std::make_unique<CabinetPedal>();
            prepared(*p);
            param(*p, "Mic Blend")->set(blend);
            param(*p, "Room")->set(0.0f);
            for (int i = 0; i < 4; ++i)
            {
                render(*p);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return p;
        };
        auto micOnlyA = reference(0.0f), micOnlyB = reference(100.0f);
        auto a = render(*micOnlyA), b = render(*micOnlyB);

        auto correlation = [](const std::vector<float>& x, const std::vector<float>& y)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (size_t n = 0; n < std::min(x.size(), y.size()); ++n)
            {
                xy += static_cast<double>(x[n]) * y[n];
                xx += static_cast<double>(x[n]) * x[n];
                yy += static_cast<double>(y[n]) * y[n];
            }
            return xy / std::sqrt(std::max(xx * yy, 1.0e-30));
        };

        CabinetPedal spreadPedal;
        prepared(spreadPedal);
        param(spreadPedal, "Room")->set(0.0f);
        for (int i = 0; i < 4; ++i)
        {
            render(spreadPedal);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Default (Mic Spread 0): both mics blended into both sides - centered, as before.
        auto centered = render(spreadPedal);
        double centeredDiff = 0.0;
        for (size_t n = 0; n < centered.left.size(); ++n)
            centeredDiff = std::max(centeredDiff, static_cast<double>(std::abs(centered.left[n] - centered.right[n])));
        check(centeredDiff < 1.0e-6, "Mic Spread 0 is centered: left and right are the same signal");

        param(spreadPedal, "Mic Spread")->set(100.0f);
        auto panned = render(spreadPedal);
        auto leftIsA = correlation(panned.left, a.left), rightIsB = correlation(panned.right, b.right);
        std::printf("  Mic Spread 100: left vs Mic A alone %.5f, right vs Mic B alone %.5f, side/mid %+.1f dB (Room 0%%)\n",
                     leftIsA, rightIsB, sideToMidDb(panned));
        check(leftIsA > 0.9999, "at Mic Spread 100 the left output is exactly Mic A");
        check(rightIsB > 0.9999, "and the right output is exactly Mic B");
        check(sideToMidDb(panned) > -15.0, "with no room at all, that is real stereo width (the mics differ) - the centered default reads -inf");

        // Mono: (L + R)/2 is an even blend of the two mics - no notch of its own.
        std::vector<float> mono(panned.left.size()), evenBlend(panned.left.size());
        for (size_t n = 0; n < mono.size(); ++n)
        {
            mono[n] = 0.5f * (panned.left[n] + panned.right[n]);
            evenBlend[n] = 0.5f * (a.left[n] + b.right[n]);
        }
        check(correlation(mono, evenBlend) > 0.9999, "summed to mono it is exactly an even blend of the two mics");

        // Width still works on top of it.
        param(spreadPedal, "Width")->set(0.0f);
        auto narrowed = render(spreadPedal);
        double monoDiff = 0.0;
        for (size_t n = 0; n < narrowed.left.size(); ++n)
            monoDiff = std::max(monoDiff, static_cast<double>(std::abs(narrowed.left[n] - narrowed.right[n])));
        check(monoDiff < 1.0e-6, "and Width 0 still collapses it to mono");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
