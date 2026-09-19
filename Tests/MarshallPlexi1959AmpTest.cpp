// End-to-end check of the complete Marshall 1959HW (MarshallPlexi1959Amp): the
// preamp, the power amp and the pickup-loading filter running as one coupled loop.
// Each part has its own suite; this checks that they are wired together right and
// that the whole behaves as the real amp does at its speaker terminals.
//
//  1. the channels: High Treble is bright, Normal full; Volumes are monotonic;
//  2. Presence opens the top end through the coupled feedback loop;
//  3. the Low jack is 6dB down, and with a pickup selected it is ALSO darker by
//     the amount the pickup-loading model predicts (and the High jack is not);
//  4. the transformer tap: on a matched cabinet a 4 ohm tap swings half the
//     voltage of a 16 ohm tap for the same power;
//  5. the speaker model reaches the loop (its resonance shows through at the
//     speaker terminals, more with Presence up);
//  6. it puts out real amplifier voltage at guitar level (tens of volts);
//  7. it is well behaved: finite and bounded with every control moving, the
//     result does not depend on the block size or the host sample rate, and
//     reset() gives a replay-identical amp.
//
// Levels are Hann-windowed lock-ins against the same measurement of the input.

#include "../Source/dsp/MarshallPlexi1959Amp.h"

#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using Cx = std::complex<double>;
    using Amp = MarshallPlexi1959Amp;
    using Routing = MarshallPlexiPreamp::Routing;

    struct Settings
    {
        Routing routing = Routing::ChannelII;
        bool low = false;
        Amp::Pickup pickup = Amp::Pickup::Off;
        float volI = 0.5f, volII = 0.5f, treble = 0.5f, middle = 0.5f, bass = 0.5f, presence = 0.5f;
        double tap = 16.0;
        AmpSpeakerLoad::Speaker speaker {};
    };

    void apply(Amp& amp, const Settings& s)
    {
        amp.setRouting(s.routing);
        amp.setLowInput(s.low);
        amp.setPickup(s.pickup);
        amp.setVolumeI(s.volI); amp.setVolumeII(s.volII);
        amp.setTreble(s.treble); amp.setMiddle(s.middle); amp.setBass(s.bass);
        amp.setPresence(s.presence);
        amp.setImpedanceTap(s.tap);
        amp.setSpeaker(s.speaker);
    }

    Cx tone(const std::vector<float>& x, double f, double sr)
    {
        Cx sum = 0.0;
        auto n = static_cast<double>(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1.0));
            sum += w * static_cast<double>(x[i]) * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(i) / sr);
        }
        return sum * 2.0 / (n * 0.5);
    }

    struct Run { std::vector<float> in, out; };

    Run run(const Settings& s, double f, double volts, double sr = 48000.0, double seconds = 0.3, double settle = 0.15)
    {
        Amp amp(sr);
        apply(amp, s);
        auto ns = static_cast<int>(settle * sr), n = static_cast<int>(seconds * sr);
        std::vector<float> in(static_cast<size_t>(ns + n)), out(static_cast<size_t>(ns + n));
        for (int i = 0; i < ns + n; ++i)
            in[static_cast<size_t>(i)] = static_cast<float>(volts * std::sin(2.0 * M_PI * f * i / sr));
        amp.processBlock(in.data(), out.data(), ns + n);
        return { std::vector<float>(in.begin() + ns, in.end()), std::vector<float>(out.begin() + ns, out.end()) };
    }

    double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

    // Gain in dB at f (output over input, measured the same way).
    double gainDb(const Settings& s, double f, double volts, double sr = 48000.0)
    {
        auto r = run(s, f, volts, sr);
        return db(std::abs(tone(r.out, f, sr)) / std::abs(tone(r.in, f, sr)));
    }

    double peakOf(const std::vector<float>& v)
    {
        double p = 0.0;
        for (auto y : v) p = std::max(p, static_cast<double>(std::abs(y)));
        return p;
    }

    constexpr double tiny = 0.001; // 1mV: every tube in its linear region
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // --- 1. Channels and Volumes. ---
    std::printf("=== Channels and Volumes, at the speaker ===\n");
    {
        Settings s; s.volI = s.volII = 0.7f;
        s.routing = Routing::ChannelI;
        auto tiltI = gainDb(s, 4000.0, tiny) - gainDb(s, 100.0, tiny);
        s.routing = Routing::ChannelII;
        auto tiltII = gainDb(s, 4000.0, tiny) - gainDb(s, 100.0, tiny);
        std::printf("  4kHz vs 100Hz: High Treble %+.1f dB, Normal %+.1f dB\n", tiltI, tiltII);
        check(tiltI > tiltII + 15.0 && tiltI > 0.0 && tiltII < 0.0, "High Treble is bright (>15dB brighter than Normal, which tilts full)");

        for (auto rt : { Routing::ChannelI, Routing::ChannelII })
        {
            double prev = -1.0;
            bool mono = true;
            for (float v : { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
            {
                Settings t; t.routing = rt;
                (rt == Routing::ChannelI ? t.volI : t.volII) = v;
                (rt == Routing::ChannelI ? t.volII : t.volI) = 0.0f;
                auto r = run(t, 1000.0, tiny);
                auto level = std::abs(tone(r.out, 1000.0, 48000.0));
                mono &= level > prev;
                prev = level;
            }
            check(mono, rt == Routing::ChannelI ? "Volume I raises the output monotonically" : "Volume II raises the output monotonically");
        }
    }
    std::printf("\n");

    // --- 2. Presence. ---
    std::printf("=== Presence through the coupled feedback loop ===\n");
    {
        Settings s; s.volII = 0.7f;
        s.presence = 0.0f;
        auto top0 = gainDb(s, 4000.0, tiny) - gainDb(s, 500.0, tiny);
        s.presence = 1.0f;
        auto top1 = gainDb(s, 4000.0, tiny) - gainDb(s, 500.0, tiny);
        std::printf("  4kHz relative to 500Hz: Presence 0 %+.1f dB, Presence 1 %+.1f dB\n", top0, top1);
        check(top1 > top0 + 6.0, "Presence up opens the top end (>6dB)");
    }
    std::printf("\n");

    // --- 3. The Low jack and the pickup. ---
    std::printf("=== Low jack: the 6dB pad, and the pickup's loading ===\n");
    {
        Settings s; s.volII = 0.7f;
        auto high = gainDb(s, 1000.0, tiny);
        s.low = true;
        auto low = gainDb(s, 1000.0, tiny);
        std::printf("  1kHz, no pickup model: Low %+.2f dB against High\n", low - high);
        check(std::abs((low - high) + 6.02) < 0.3, "the Low jack is 6dB down");

        for (auto p : { Amp::Pickup::SingleCoil, Amp::Pickup::Humbucker })
        {
            auto setup = PickupLoading::setupFor(p == Amp::Pickup::SingleCoil ? PickupLoading::singleCoil() : PickupLoading::humbucker(), 136.0e3);
            Settings off; off.volII = 0.7f; off.low = true;
            Settings on = off; on.pickup = p;
            // The preamp's own response is the same either way, so the DIFFERENCE
            // between the two is the pickup filter, whatever the preamp does.
            for (double f : { 2000.0, 3000.0, 5000.0 })
            {
                auto measuredDb = gainDb(on, f, tiny) - gainDb(off, f, tiny);
                auto predictedDb = db(std::abs(PickupLoading::response(setup, f)));
                std::printf("  %s, Low jack, %.0f Hz: %+.2f dB (model %+.2f)\n", p == Amp::Pickup::SingleCoil ? "single coil" : "humbucker  ", f, measuredDb, predictedDb);
                check(std::abs(measuredDb - predictedDb) < 0.6, "the amp applies the pickup model's loading");
            }
            Settings highOn = on; highOn.low = false;
            Settings highOff = off; highOff.low = false;
            auto delta = gainDb(highOn, 3000.0, tiny) - gainDb(highOff, 3000.0, tiny);
            check(std::abs(delta) < 0.2, "and the High jack (the reference load) is left alone");
        }
    }
    std::printf("\n");

    // --- 4. The tap. ---
    std::printf("=== Transformer tap, matched cabinet ===\n");
    {
        auto peakAt = [&](double tap) {
            Settings s; s.routing = Routing::Jumpered; s.volI = 0.8f; s.volII = 0.8f;
            s.tap = tap;
            AmpSpeakerLoad::Speaker sp;
            s.speaker = sp;
            return peakOf(run(s, 400.0, 0.8, 48000.0, 0.15, 0.15).out);
        };
        auto v16 = peakAt(16.0), v4 = peakAt(4.0);
        std::printf("  hard-driven peak at the speaker: %.1f V on 16 ohms, %.1f V on 4 ohms (ratio %.2f, ideal 0.50)\n", v16, v4, v4 / v16);
        check(v16 > 40.0, "tens of volts of real amplifier output at guitar level");
        check(v4 / v16 > 0.4 && v4 / v16 < 0.65, "a 4 ohm tap on a 4 ohm cabinet swings about half the voltage of 16 on 16 (the same power)");

        // However hard it is driven, the output cannot exceed what the stage can
        // physically swing: the conducting tube's plate cannot go below ~0V, so the
        // other cannot rise above ~2*B+ (see MarshallPlexiPowerAmp's plate-swing
        // clamp) - about 70V on a 16 ohm tap, 35V on 4 ohms. 3V of guitar into
        // wide-open Volumes is far beyond any playing.
        auto extreme = [&](double tap) {
            Settings s; s.routing = Routing::Jumpered; s.volI = 1.0f; s.volII = 1.0f; s.tap = tap;
            return peakOf(run(s, 400.0, 3.0, 48000.0, 0.15, 0.15).out);
        };
        auto e16 = extreme(16.0), e4 = extreme(4.0);
        std::printf("  3V of guitar into Volumes 10 and 10: peak %.1f V on 16 ohms, %.1f V on 4 ohms\n", e16, e4);
        check(e16 < 85.0 && e4 < 43.0, "the output stays within the stage's physical swing (< 85V / < 43V) - no flyback spikes or runaway");
    }
    std::printf("\n");

    // --- 5. The speaker reaches the loop. ---
    std::printf("=== The speaker's impedance curve, at the speaker terminals ===\n");
    {
        auto bump = [&](double fs0) {
            Settings s; s.volII = 0.7f; s.presence = 1.0f;
            s.speaker.fs = fs0;
            return gainDb(s, 85.0, tiny) - gainDb(s, 400.0, tiny);
        };
        auto matched = bump(85.0), away = bump(200.0);
        std::printf("  85Hz vs 400Hz gain, Presence 1: %+.1f dB with a speaker resonant at 85Hz, %+.1f dB at 200Hz\n", matched, away);
        check(matched > away + 1.0, "a speaker that resonates at 85Hz raises the output there (the feedback loop sees its impedance)");
    }
    std::printf("\n");

    // --- 6. Robustness. ---
    std::printf("=== Robustness ===\n");
    {
        Amp amp(48000.0);
        unsigned seed = 77u;
        bool ok = true;
        std::vector<float> in(128), out(128);
        for (int block = 0; block < 500; ++block)
        {
            Settings s;
            s.routing = static_cast<Routing>(block % 3);
            s.low = block % 2 == 0;
            s.pickup = static_cast<Amp::Pickup>(block % 3);
            s.volI = static_cast<float>(block % 7) / 6.0f; s.volII = static_cast<float>(block % 5) / 4.0f;
            s.treble = static_cast<float>(block % 4) / 3.0f; s.middle = static_cast<float>(block % 3) / 2.0f; s.bass = static_cast<float>(block % 6) / 5.0f;
            s.presence = static_cast<float>(block % 5) / 4.0f;
            s.tap = block % 3 == 0 ? 4.0 : (block % 3 == 1 ? 8.0 : 16.0);
            s.speaker.fs = 70.0 + static_cast<double>(block % 9) * 15.0;
            apply(amp, s);
            for (auto& x : in) { seed = seed * 1664525u + 1013904223u; x = 1.0f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f); }
            amp.processBlock(in.data(), out.data(), 128);
            for (auto y : out) ok &= std::isfinite(y) && std::abs(y) < 400.0f;
        }
        check(ok, "1V noise with every control, the jack, pickup, tap and speaker changing every 128 samples stays finite and bounded");

        // Block size makes no difference.
        Settings s; s.routing = Routing::Jumpered; s.volI = s.volII = 0.6f; s.pickup = Amp::Pickup::Humbucker; s.low = true;
        std::vector<float> input(24000), whole(24000), pieces(24000);
        for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / 48000.0));
        Amp a(48000.0), b(48000.0);
        apply(a, s); apply(b, s);
        a.processBlock(input.data(), whole.data(), 24000);
        for (int i = 0; i < 24000; i += 64) b.processBlock(input.data() + i, pieces.data() + i, std::min(64, 24000 - i));
        double diff = 0.0;
        for (size_t i = 0; i < whole.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(whole[i] - pieces[i])));
        std::printf("  one 24000-sample block vs 64-sample blocks: worst difference %.2e V\n", diff);
        check(diff < 1.0e-4, "the block size doesn't change the result");

        // Reset gives a replay-identical amp.
        std::vector<float> again(24000);
        a.reset();
        a.processBlock(input.data(), again.data(), 24000);
        double replay = 0.0;
        for (size_t i = 0; i < whole.size(); ++i) replay = std::max(replay, static_cast<double>(std::abs(whole[i] - again[i])));
        std::printf("  replay after reset(): worst difference %.2e V\n", replay);
        check(replay < 1.0e-4, "reset() returns to a clean slate");
    }
    std::printf("\n");

    // --- 7. Host sample rate. ---
    std::printf("=== Sample-rate independence ===\n");
    {
        Settings s; s.volII = 0.7f;
        double lvl[3], tilt[3];
        int i = 0;
        for (auto sr : { 44100.0, 48000.0, 96000.0 })
        {
            lvl[i] = gainDb(s, 1000.0, tiny, sr);
            tilt[i] = gainDb(s, 4000.0, tiny, sr) - gainDb(s, 100.0, tiny, sr);
            ++i;
        }
        std::printf("  1kHz gain %.2f / %.2f / %.2f dB, 4kHz-vs-100Hz tilt %.2f / %.2f / %.2f dB (44.1 / 48 / 96 kHz)\n", lvl[0], lvl[1], lvl[2], tilt[0], tilt[1], tilt[2]);
        check(std::abs(lvl[0] - lvl[1]) < 0.5 && std::abs(lvl[2] - lvl[1]) < 0.5, "1kHz gain agrees across sample rates (0.5dB)");
        check(std::abs(tilt[0] - tilt[1]) < 1.0 && std::abs(tilt[2] - tilt[1]) < 1.0, "spectral tilt agrees across sample rates (1dB)");
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
