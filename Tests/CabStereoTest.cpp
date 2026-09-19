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

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
