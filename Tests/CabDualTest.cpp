// Verify the real, generated cab IRs behave the way dual-cab mode assumes:
//  - different cabs genuinely have different frequency responses (so a
//    left/right pair is a real tonal difference, not two copies);
//  - identical cabs sum to exactly themselves (dual mode with the same cab
//    changes nothing);
//  - two DIFFERENT cabs summed to the center produce comb filtering - dips
//    below what either cab has alone - while a hard-panned pair, having
//    nothing summed, has no such interaction;
//  - inverting one mic of a blend measurably thins the low end.
//
// This links the real CabImpulseResponse generator (juce_audio_basics
// only, no audio device, no GUI), so it tests the actual IRs the pedal
// convolves with, not a stand-in.

#include "../Source/CabImpulseResponse.h"

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

    // One channel of a generated mic IR.
    std::vector<float> ir(int cabType, int micType, float position, int channel = 0)
    {
        auto buffer = CabImpulseResponse::generateMicIR(sampleRate, cabType, micType, position);
        std::vector<float> out(static_cast<size_t>(buffer.getNumSamples()));
        for (size_t n = 0; n < out.size(); ++n)
            out[n] = buffer.getSample(channel, static_cast<int>(n));
        return out;
    }

    std::complex<double> response(const std::vector<float>& h, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < h.size(); ++n)
            sum += static_cast<double>(h[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return sum;
    }

    double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

    // Log-spaced test frequencies across the guitar-cab range.
    std::vector<double> bins()
    {
        std::vector<double> f;
        for (double x = 150.0; x <= 6000.0; x *= 1.03)
            f.push_back(x);
        return f;
    }

    std::vector<float> sum(const std::vector<float>& a, const std::vector<float>& b, float wa = 0.5f, float wb = 0.5f)
    {
        std::vector<float> out(std::min(a.size(), b.size()));
        for (size_t n = 0; n < out.size(); ++n)
            out[n] = wa * a[n] + wb * b[n];
        return out;
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    constexpr int v30 = CabImpulseResponse::cabFourByTwelveV30;
    constexpr int fenderOneByTen = CabImpulseResponse::cabFenderOneByTen;
    constexpr int dynamic = CabImpulseResponse::micDynamic;
    constexpr int ribbon = CabImpulseResponse::micRibbon;
    const auto freqs = bins();

    auto cabA = ir(v30, dynamic, 0.3f);
    auto cabB = ir(fenderOneByTen, dynamic, 0.3f);

    // --- 1. Different cabs really differ. ---
    std::printf("=== Different cabs have genuinely different responses ===\n");
    {
        double sumAbs = 0.0, maxAbs = 0.0;
        for (auto f : freqs)
        {
            auto d = std::abs(db(std::abs(response(cabA, f))) - db(std::abs(response(cabB, f))));
            sumAbs += d;
            maxAbs = std::max(maxAbs, d);
        }
        auto meanAbs = sumAbs / static_cast<double>(freqs.size());
        std::printf("  4x12 V30 vs Fender 1x10: mean |difference| %.1f dB, worst %.1f dB\n", meanAbs, maxAbs);
        check(meanAbs > 2.0 && maxAbs > 6.0, "a V30 4x12 and a Fender 1x10 differ clearly");
    }

    // --- 2. Identical cabs sum to themselves. ---
    std::printf("\n=== Identical cabs: summing changes nothing ===\n");
    {
        auto same = sum(cabA, cabA);
        double worst = 0.0;
        for (auto f : freqs)
            worst = std::max(worst, std::abs(db(std::abs(response(same, f))) - db(std::abs(response(cabA, f)))));
        std::printf("  worst deviation from the single cab: %.4f dB\n", worst);
        check(worst < 0.01, "same cab on both sides is indistinguishable from one cab");
    }

    // --- 3. Comb filtering: centered sum vs hard-panned. ---
    // "Dip" = how far the summed response falls below the average of the
    // two cabs' own responses (in dB) at its worst frequency. A single cab
    // (or a hard-panned pair, where nothing is summed) can't dip below
    // itself; two different cabs summed can, where their phases oppose.
    std::printf("\n=== Two different cabs summed to center: comb filtering ===\n");
    {
        auto centered = sum(cabA, cabB);
        double worstDip = 0.0;
        double worstFreq = 0.0;
        int notches = 0;
        for (auto f : freqs)
        {
            auto meanOfCabs = 0.5 * (db(std::abs(response(cabA, f))) + db(std::abs(response(cabB, f))));
            auto dip = db(std::abs(response(centered, f))) - meanOfCabs;
            if (dip < worstDip) { worstDip = dip; worstFreq = f; }
            if (dip < -6.0) ++notches;
        }
        std::printf("  centered V30 + Fender 1x10: deepest dip %.1f dB at %.0fHz, %d of %zu bins more than 6dB down\n",
                     worstDip, worstFreq, notches, freqs.size());
        check(worstDip < -6.0, "summing two different cabs to center creates real notches (comb filtering)");

        // Hard-panned: each side is one cab alone - by construction its
        // response IS that cab's, so there is nothing to dip below.
        std::printf("  hard-panned: each side is one cab alone, so 0 dB of interaction by construction\n");
    }

    // --- 4. Polarity: inverting one mic of a blend thins the low end. ---
    std::printf("\n=== Polarity: inverting Mic B thins the low end of the blend ===\n");
    {
        auto micA = ir(v30, dynamic, 0.3f);
        auto micB = ir(v30, ribbon, 0.7f);
        auto normal = sum(micA, micB, 0.5f, 0.5f);
        auto inverted = sum(micA, micB, 0.5f, -0.5f);

        auto bandLevel = [&](const std::vector<float>& h)
        {
            double energy = 0.0;
            int count = 0;
            for (double f = 100.0; f <= 400.0; f *= 1.05, ++count)
            {
                auto m = std::abs(response(h, f));
                energy += m * m;
            }
            return 10.0 * std::log10(energy / count);
        };
        auto normalDb = bandLevel(normal), invertedDb = bandLevel(inverted);
        std::printf("  100-400Hz level: normal %.1f dB, Mic B inverted %.1f dB (%.1f dB change)\n",
                     normalDb, invertedDb, invertedDb - normalDb);
        check(invertedDb < normalDb - 3.0, "inverting Mic B drops the low end by at least 3 dB");
    }

    // --- 5. Stereo structure of the generated IRs. ---
    // The mic IR is a mono source (both channels identical, for EVERY cab
    // and mic - it used to carry a 0.3 ms left/right offset that notched
    // the mono sum at ~1.7kHz); the room IR is where the two channels
    // genuinely differ, the way two spaced room mics do: fairly coherent
    // in the lows, decorrelated above.
    std::printf("\n=== Mic IRs are centered (identical channels) for every cab and mic ===\n");
    {
        double worst = 0.0;
        for (int cabType = 0; cabType < CabImpulseResponse::numCabTypes; ++cabType)
            for (int micType = 0; micType < CabImpulseResponse::numMicTypes; ++micType)
                for (float position : { 0.0f, 0.5f, 1.0f })
                {
                    auto left = ir(cabType, micType, position, 0);
                    auto right = ir(cabType, micType, position, 1);
                    for (size_t n = 0; n < left.size(); ++n)
                        worst = std::max(worst, static_cast<double>(std::abs(left[n] - right[n])));
                }
        std::printf("  largest left/right difference across 8 cabs x 2 mics x 3 positions: %.2g\n", worst);
        check(worst == 0.0, "left and right are bit-identical everywhere");
    }

    std::printf("\n=== Room IR: two genuinely different channels ===\n");
    {
        auto bandCorrelation = [](const juce::AudioBuffer<float>& room, double lo, double hi)
        {
            std::vector<float> l(room.getReadPointer(0), room.getReadPointer(0) + room.getNumSamples());
            std::vector<float> r(room.getReadPointer(1), room.getReadPointer(1) + room.getNumSamples());
            for (auto* channel : { &l, &r })
            {
                juce::IIRFilter highpass, lowpass;
                highpass.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, lo));
                lowpass.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, hi));
                highpass.processSamples(channel->data(), static_cast<int>(channel->size()));
                lowpass.processSamples(channel->data(), static_cast<int>(channel->size()));
            }
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (size_t n = 0; n < l.size(); ++n)
            {
                lr += static_cast<double>(l[n]) * r[n];
                ll += static_cast<double>(l[n]) * l[n];
                rr += static_cast<double>(r[n]) * r[n];
            }
            return lr / std::sqrt(std::max(ll * rr, 1.0e-30));
        };

        for (bool studio : { false, true })
        {
            auto room = CabImpulseResponse::generateRoomIR(sampleRate, studio);
            auto low = bandCorrelation(room, 150.0, 400.0);
            auto mid = bandCorrelation(room, 400.0, 900.0);
            auto high1 = bandCorrelation(room, 900.0, 2000.0);
            auto high2 = bandCorrelation(room, 2000.0, 4000.0);
            std::printf("  %s: correlation 150-400Hz %+.2f, 400-900Hz %+.2f, 0.9-2kHz %+.2f, 2-4kHz %+.2f\n",
                         studio ? "Studio" : "Live", low, mid, high1, high2);
            check(std::abs(mid) < 0.35 && std::abs(high1) < 0.35 && std::abs(high2) < 0.35,
                  "decorrelated above 400Hz (like two spaced room mics)");
            check(low < 0.85, "and not a copy even in the lows");
        }
    }

    // --- 6. Mixed speakers (generateMixedMicIR). ---
    std::printf("\n=== Mixed speakers: a cab with no second speaker is untouched ===\n");
    {
        constexpr int greenback = CabImpulseResponse::cabFourByTwelveGreenback;
        auto plain = CabImpulseResponse::generateMicIR(sampleRate, v30, dynamic, 0.3f);

        auto identical = [&](const juce::AudioBuffer<float>& other)
        {
            if (other.getNumSamples() != plain.getNumSamples() || other.getNumChannels() != plain.getNumChannels())
                return false;
            for (int ch = 0; ch < plain.getNumChannels(); ++ch)
                for (int n = 0; n < plain.getNumSamples(); ++n)
                    if (other.getSample(ch, n) != plain.getSample(ch, n))
                        return false;
            return true;
        };

        check(identical(CabImpulseResponse::generateMixedMicIR(sampleRate, v30, -1, 0.5f, dynamic, 0.3f)),
              "no second speaker: bit-identical to the plain cab");
        check(identical(CabImpulseResponse::generateMixedMicIR(sampleRate, v30, greenback, 0.0f, dynamic, 0.3f)),
              "mix 0: bit-identical to the plain cab");
        check(identical(CabImpulseResponse::generateMixedMicIR(sampleRate, v30, v30, 0.5f, dynamic, 0.3f)),
              "second speaker = the cab's own: bit-identical to the plain cab");

        auto allSecond = CabImpulseResponse::generateMixedMicIR(sampleRate, v30, greenback, 1.0f, dynamic, 0.3f);
        auto greenbackPlain = CabImpulseResponse::generateMicIR(sampleRate, greenback, dynamic, 0.3f);
        double worst = 0.0;
        for (int n = 0; n < greenbackPlain.getNumSamples(); ++n)
            worst = std::max(worst, static_cast<double>(std::abs(allSecond.getSample(0, n) - greenbackPlain.getSample(0, n))));
        check(worst < 1.0e-6, "mix 100: the second speaker's own IR");
    }

    std::printf("\n=== Mixed speakers: the mix moves the tone between the two speakers ===\n");
    {
        constexpr int greenback = CabImpulseResponse::cabFourByTwelveGreenback;

        auto bandDb = [](const juce::AudioBuffer<float>& buffer, double lo, double hi)
        {
            std::vector<float> x(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
            double sum = 0.0;
            int count = 0;
            for (double f = lo; f <= hi; f *= 1.05, ++count)
                sum += db(std::abs(response(x, f)));
            return sum / count;
        };

        // The Greenback's own ~600Hz hump vs the V30's 2.5-3.5kHz presence.
        std::printf("  mix   warm 500-700Hz   bite 2.5-3.5kHz   (V30 -> Greenback)\n");
        double warm[5], bite[5];
        int i = 0;
        for (float mix : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto ir = CabImpulseResponse::generateMixedMicIR(sampleRate, v30, greenback, mix, dynamic, 0.3f);
            warm[i] = bandDb(ir, 500.0, 700.0);
            bite[i] = bandDb(ir, 2500.0, 3500.0);
            std::printf("  %.2f   %+6.1f dB        %+6.1f dB\n", mix, warm[i], bite[i]);
            ++i;
        }

        bool warmRises = true, biteFalls = true;
        for (int k = 1; k < 5; ++k)
        {
            warmRises &= warm[k] > warm[k - 1];
            biteFalls &= bite[k] < bite[k - 1];
        }
        check(warmRises, "more Greenback = more of its 600Hz warmth, at every step");
        check(biteFalls, "more Greenback = less of the V30's presence bite, at every step");
        check(warm[2] > warm[0] && warm[2] < warm[4] && bite[2] < bite[0] && bite[2] > bite[4],
              "an even mix sits between the two speakers on both");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
