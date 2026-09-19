// End-to-end check of Spk Push through the real Cabinet pedal (the physics
// itself is tested in SpeakerCompressionTest).
//
// Trick used throughout: a single-impulse WAV loaded as the cab's IR makes
// the cab itself essentially transparent, so any harmonic distortion in
// the output was made by the speaker model - and the IR being the same for
// every "Cab" setting isolates the per-cab headroom too.
//
//  1. Push = 0 leaves a hot bass note undistorted; Push > 0 distorts it;
//  2. it is level-dependent: a quiet note stays clean even at full Push;
//  3. it applies to a real loaded IR (the point: a static IR can't do this
//     on its own);
//  4. the Cab setting sets the speaker's headroom (Greenback breaks up
//     before V30), and a mixed second speaker blends the two headrooms;
//  5. in dual-cab mode both sides get it, each with its own cab's headroom.

#include "CabPedalTestKit.h"

#include <complex>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace CabTest;

namespace
{
    constexpr int sineBlocks = 40;
    constexpr int measureSamples = 12000; // 0.25 s = exactly 25 cycles of 100 Hz (see exactMagnitude)

    // Steady 100 Hz through the pedal; returns the last 0.25 s of each channel.
    Rendered sine(CabinetPedal& pedal, double amplitude)
    {
        std::vector<float> l(blockSize), r(blockSize);
        Rendered out;
        long sampleIndex = 0;
        for (int b = 0; b < sineBlocks; ++b)
        {
            for (int n = 0; n < blockSize; ++n, ++sampleIndex)
            {
                auto s = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 100.0 * static_cast<double>(sampleIndex) / sampleRate));
                l[static_cast<size_t>(n)] = r[static_cast<size_t>(n)] = s;
            }
            float* channels[2] = { l.data(), r.data() };
            pedal.process(channels, 2, blockSize);
            out.left.insert(out.left.end(), l.begin(), l.end());
            out.right.insert(out.right.end(), r.begin(), r.end());
        }
        out.left.erase(out.left.begin(), out.left.end() - measureSamples);
        out.right.erase(out.right.begin(), out.right.end() - measureSamples);
        return out;
    }

    // Magnitude at EXACTLY `freq`. The window is a whole number of cycles
    // (12000 samples = 25 cycles of 100 Hz; bins are 4 Hz apart, so 100,
    // 200, 300 Hz all land dead on a bin), so a linear system reads ~0
    // at the harmonics. The project's Goertzel helper offsets its bin by
    // half a step, which lets a pure tone leak ~1% into far-away bins -
    // enough to swamp what this test is looking for.
    double exactMagnitude(const std::vector<float>& x, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return std::abs(sum) * 2.0 / static_cast<double>(x.size());
    }

    double thirdHarmonic(const std::vector<float>& x)
    {
        return exactMagnitude(x, 300.0) / std::max(1.0e-12, exactMagnitude(x, 100.0));
    }

    // A pedal whose cab is (essentially) transparent: Mic A carries the
    // single-impulse IR, Blend 0, no room, so the output is the speaker
    // model's work alone.
    void makeTransparent(CabinetPedal& p, const juce::File& impulse)
    {
        prepared(p);
        p.loadImpulseResponseFile(0, impulse);
        param(p, "Mic Blend")->set(0.0f);
        param(p, "Room")->set(0.0f);
        param(p, "Air")->set(0.0f);
        param(p, "Low Cut")->set(40.0f);
        param(p, "High Cut")->set(12000.0f);
        waitUntil(p, [&](const Rendered& x) { return isClean(x.left); });
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("csim_cab_push_test");
    dir.createDirectory();
    auto impulse = makeIR(dir, "impulse.wav", { { 0, 1.0f }, { 30, 0.01f } });

    CabinetPedal pedal;
    makeTransparent(pedal, impulse);

    std::printf("=== Push = 0 is clean; Push distorts a hot bass note ===\n");
    double thirdOff = 0.0, thirdOn = 0.0;
    {
        thirdOff = thirdHarmonic(sine(pedal, 0.6).left);
        param(pedal, "Spk Push")->set(100.0f);
        thirdOn = thirdHarmonic(sine(pedal, 0.6).left);
        std::printf("  100Hz at 0.6: 3rd harmonic %.4f at Push 0, %.4f at Push 100%%\n", thirdOff, thirdOn);
        check(thirdOff < 0.005, "with Push at 0 the note is clean (the transparent cab adds nothing)");
        check(thirdOn > 0.05, "at Push 100% the speaker adds a clear 3rd harmonic");
    }

    std::printf("\n=== It is level-dependent ===\n");
    {
        param(pedal, "Spk Push")->set(100.0f);
        auto quiet = thirdHarmonic(sine(pedal, 0.03).left);
        std::printf("  at 0.03 and full Push: 3rd harmonic %.4f\n", quiet);
        check(quiet < 0.01, "a quiet note stays clean even at full Push");
        check(thirdOn > quiet * 8.0, "the hot note distorts many times more than the quiet one");
    }

    std::printf("\n=== The cab's speaker sets the headroom ===\n");
    {
        // Same transparent IR, same drive, only the Cab setting differs.
        CabinetPedal v30, greenback;
        makeTransparent(v30, impulse);
        makeTransparent(greenback, impulse);
        param(v30, "Cab")->set(0.0f);        // 4x12 V30, 60 W
        param(greenback, "Cab")->set(1.0f);  // 4x12 Greenback, 25 W
        for (auto* p : { &v30, &greenback })
            param(*p, "Spk Push")->set(50.0f);

        auto v30Third = thirdHarmonic(sine(v30, 0.5).left);
        auto greenbackThird = thirdHarmonic(sine(greenback, 0.5).left);
        std::printf("  Push 50%%, 100Hz at 0.5: V30 3rd %.4f, Greenback 3rd %.4f\n", v30Third, greenbackThird);
        check(greenbackThird > v30Third * 1.5, "the 25 W Greenback breaks up clearly sooner than the 60 W V30");

        // Mixed speakers: V30 with Greenback mixed in blends the two headrooms.
        CabinetPedal mixed;
        makeTransparent(mixed, impulse);
        param(mixed, "Cab")->set(0.0f);
        param(mixed, "Spk 2")->set(2.0f);    // list: None, V30, Greenback...
        param(mixed, "Spk Push")->set(50.0f);
        param(mixed, "Spk Mix")->set(50.0f);
        auto halfway = thirdHarmonic(sine(mixed, 0.5).left);
        param(mixed, "Spk Mix")->set(100.0f);
        auto allGreenback = thirdHarmonic(sine(mixed, 0.5).left);
        std::printf("  V30 + Greenback: mix 50%% 3rd %.4f, mix 100%% 3rd %.4f (plain Greenback %.4f)\n",
                     halfway, allGreenback, greenbackThird);
        check(halfway > v30Third && halfway < greenbackThird, "a 50/50 mix breaks up between the two");
        check(std::abs(allGreenback - greenbackThird) < greenbackThird * 0.05, "a 100% mix has exactly the Greenback's headroom");
    }

    std::printf("\n=== Dual cab: both sides get it, each with its own cab's headroom ===\n");
    {
        // Left V30, right Greenback (Cab R list: Same, V30, Greenback...). Both cabs' IRs
        // are the transparent impulse, so only the headroom differs.
        CabinetPedal dual;
        prepared(dual);
        dual.loadImpulseResponseFile(0, impulse);
        dual.loadImpulseResponseFile(2, impulse);
        param(dual, "Mic Blend")->set(0.0f);
        param(dual, "Room")->set(0.0f);
        param(dual, "Air")->set(0.0f);
        param(dual, "Low Cut")->set(40.0f);
        param(dual, "High Cut")->set(12000.0f);
        param(dual, "Cab")->set(0.0f);        // V30
        param(dual, "Cab R")->set(2.0f);      // Greenback
        param(dual, "Spk Push")->set(50.0f);
        waitUntil(dual, [&](const Rendered& x) { return isClean(x.left) && isClean(x.right); });

        auto r = sine(dual, 0.5);
        auto left = thirdHarmonic(r.left), right = thirdHarmonic(r.right);
        std::printf("  left (V30) 3rd %.4f, right (Greenback) 3rd %.4f\n", left, right);
        check(left > 0.005 && right > 0.005, "both sides are pushed");
        check(right > left * 1.5, "the right (Greenback) side breaks up sooner than the left (V30) side");
    }

    dir.deleteRecursively();
    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
