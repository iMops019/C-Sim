// End-to-end check of the Cabinet's mixed-speaker feature through the real
// pedal (the IRs themselves are tested in CabDualTest):
//  1. with Spk 2 = None, Spk Mix does nothing at all;
//  2. Spk 2 with mix 0 sounds exactly like the plain cab, and mix 100
//     exactly like the second speaker's cab on its own - so the pedal is
//     really loading the blended IR, and the endpoints are honest;
//  3. an even mix lands between the two, in tone;
//  4. dual-cab: the right cab has its OWN second speaker and mix (Spk 2 R /
//     Spk Mix R), and the left cab is unaffected by them;
//  5. with Cab R at "Same as Cab", the right side mirrors the left cab's
//     mix instead of using the R pair.
//
// "Sounds exactly like" = the pedal's whole output correlates with a
// reference pedal that has that plain cab selected, above 0.999.

#include "CabPedalTestKit.h"

#include <complex>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace CabTest;

namespace
{
    // The cab-list indices used below (see the Cab / Cab R / Spk 2 labels).
    constexpr float cabV30 = 0.0f, cabGreenback = 1.0f, cabFenderOneByTen = 4.0f;
    // Spk 2 and Cab R lists have an extra first entry ("None" / "Same as Cab").
    constexpr float listV30 = 1.0f, listGreenback = 2.0f, listFenderOneByTen = 5.0f;

    void neutral(CabinetPedal& p)
    {
        param(p, "Mic Blend")->set(0.0f); // Mic A only, so one IR decides the sound
        param(p, "Room")->set(0.0f);
    }

    // Let asynchronous IR swaps land, then return the last render.
    Rendered stable(CabinetPedal& p)
    {
        Rendered r;
        for (int i = 0; i < 6; ++i)
        {
            r = render(p);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        return r;
    }

    double correlation(const std::vector<float>& a, const std::vector<float>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t n = 0; n < std::min(a.size(), b.size()); ++n)
        {
            ab += static_cast<double>(a[n]) * b[n];
            aa += static_cast<double>(a[n]) * a[n];
            bb += static_cast<double>(b[n]) * b[n];
        }
        return ab / std::sqrt(std::max(aa * bb, 1.0e-30));
    }

    double bandDb(const std::vector<float>& x, double lo, double hi)
    {
        double sum = 0.0;
        int count = 0;
        for (double f = lo; f <= hi; f *= 1.05, ++count)
        {
            std::complex<double> s = 0.0;
            for (size_t n = 0; n < x.size(); ++n)
                s += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(n) / sampleRate);
            sum += 20.0 * std::log10(std::max(std::abs(s), 1.0e-12));
        }
        return sum / count;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // Reference sounds: each plain cab on its own.
    CabinetPedal refV30, refGreenback;
    prepared(refV30);
    prepared(refGreenback);
    neutral(refV30);
    neutral(refGreenback);
    param(refV30, "Cab")->set(cabV30);
    param(refGreenback, "Cab")->set(cabGreenback);
    auto v30 = stable(refV30);
    auto greenback = stable(refGreenback);

    CabinetPedal pedal;
    prepared(pedal);
    neutral(pedal);
    param(pedal, "Cab")->set(cabV30);

    std::printf("=== Spk 2 = None: Spk Mix does nothing ===\n");
    {
        auto before = stable(pedal);
        param(pedal, "Spk Mix")->set(100.0f);
        auto after = stable(pedal);
        double maxDiff = 0.0;
        for (size_t n = 0; n < before.left.size(); ++n)
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(before.left[n] - after.left[n])));
        std::printf("  correlation with the plain V30: %.5f, largest change from moving Spk Mix: %.2g\n",
                     correlation(before.left, v30.left), maxDiff);
        check(correlation(before.left, v30.left) > 0.999, "the default is the plain cab");
        check(maxDiff < 1.0e-6, "Spk Mix has no effect without a second speaker");
    }

    std::printf("\n=== The endpoints are honest ===\n");
    {
        param(pedal, "Spk 2")->set(listGreenback);
        param(pedal, "Spk Mix")->set(0.0f);
        auto mix0 = stable(pedal);
        param(pedal, "Spk Mix")->set(100.0f);
        auto mix100 = stable(pedal);
        auto c0 = correlation(mix0.left, v30.left), c100 = correlation(mix100.left, greenback.left);
        std::printf("  V30 + Greenback: mix 0 vs plain V30 %.5f | mix 100 vs plain Greenback %.5f\n", c0, c100);
        check(c0 > 0.999, "mix 0 sounds exactly like the plain V30");
        check(c100 > 0.999, "mix 100 sounds exactly like the plain Greenback");
    }

    std::printf("\n=== An even mix sits between the two ===\n");
    {
        param(pedal, "Spk Mix")->set(50.0f);
        auto mix50 = stable(pedal);
        auto warm = [&](const Rendered& r) { return bandDb(r.left, 500.0, 700.0); };
        auto bite = [&](const Rendered& r) { return bandDb(r.left, 2500.0, 3500.0); };
        std::printf("  warm 500-700Hz: V30 %+.1f, mix %+.1f, Greenback %+.1f | bite 2.5-3.5kHz: V30 %+.1f, mix %+.1f, Greenback %+.1f\n",
                     warm(v30), warm(mix50), warm(greenback), bite(v30), bite(mix50), bite(greenback));
        check(warm(mix50) > warm(v30) && warm(mix50) < warm(greenback), "warmth lands between the two speakers");
        check(bite(mix50) < bite(v30) && bite(mix50) > bite(greenback), "presence bite lands between the two speakers");
    }

    std::printf("\n=== Dual cab: the right cab has its own second speaker ===\n");
    {
        // Left: plain V30. Right: a Fender 1x10 with the V30 mixed in 100%,
        // i.e. it should sound exactly like a plain V30 - on the right side.
        CabinetPedal dual;
        prepared(dual);
        neutral(dual);
        param(dual, "Cab")->set(cabV30);
        param(dual, "Cab R")->set(listFenderOneByTen);
        param(dual, "Spk 2 R")->set(listV30);
        param(dual, "Spk Mix R")->set(100.0f);
        auto r = stable(dual);
        auto rightIsV30 = correlation(r.right, v30.right);
        auto leftIsV30 = correlation(r.left, v30.left);
        std::printf("  right vs plain V30 %.5f | left vs plain V30 %.5f\n", rightIsV30, leftIsV30);
        check(rightIsV30 > 0.999, "the right cab (a Fender 1x10 mixed 100% to V30) sounds exactly like a V30");
        check(leftIsV30 > 0.999, "and the left cab is unaffected by the right cab's second speaker");

        // Without the mix the right cab really is the Fender 1x10 (so the
        // match above wasn't just both sides being the same by accident).
        param(dual, "Spk Mix R")->set(0.0f);
        auto plainFender = stable(dual);
        check(correlation(plainFender.right, v30.right) < 0.9, "at mix 0 the right cab is audibly not a V30");
    }

    std::printf("\n=== Cab R = Same as Cab: the right side mirrors the left's mix ===\n");
    {
        // A right-side real IR forces dual mode on with Cab R at "Same",
        // leaving the right SYNTHETIC mic (Mic B here) to use the left cab.
        // Put Mic B alone on the outputs (Blend 100) and load nothing but a
        // right-side IR into Mic A: the right channel then plays the right
        // synthetic Mic B, which must carry the left's V30+Greenback mix.
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("csim_cab_mixed_test");
        dir.createDirectory();
        auto silent = makeIR(dir, "quiet.wav", { { 0, 0.001f }, { 30, 0.0005f } });

        CabinetPedal mirror;
        prepared(mirror);
        param(mirror, "Room")->set(0.0f);
        param(mirror, "Mic Blend")->set(100.0f);
        param(mirror, "Cab")->set(cabV30);
        param(mirror, "Spk 2")->set(listGreenback);
        param(mirror, "Spk Mix")->set(100.0f);
        param(mirror, "Spk 2 R")->set(listV30);      // must be IGNORED: Cab R is "Same"
        param(mirror, "Spk Mix R")->set(100.0f);
        mirror.loadImpulseResponseFile(2, silent);   // Right Mic A: dual on
        param(mirror, "Mic Blend")->set(100.0f);     // (loading reset it to 0)
        param(mirror, "Air")->set(15.0f);            // (and raised Air to 45 - back to the reference's default)
        auto r = stable(mirror);

        // Both sides play their Mic B: left = V30+Greenback@100 (a Greenback),
        // right = the mirrored left cab (also a Greenback), NOT the ignored R pair (V30).
        // Compare against a Mic-B-only plain Greenback.
        CabinetPedal refB;
        prepared(refB);
        param(refB, "Room")->set(0.0f);
        param(refB, "Mic Blend")->set(100.0f);
        param(refB, "Cab")->set(cabGreenback);
        auto refGreen = stable(refB);
        auto rightMirrors = correlation(r.right, refGreen.right);
        std::printf("  right (Cab R = Same, Spk 2 R ignored) vs plain Greenback: %.5f\n", rightMirrors);
        check(rightMirrors > 0.999, "the right side used the LEFT cab's mix, not the ignored Spk 2 R");
        dir.deleteRecursively();
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
