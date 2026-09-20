// End-to-end check of the real Marshall 1959HW pedal (JUCE-coupled). The amp's
// circuit is tested on its own (MarshallToneStackTest, MarshallPlexiPreampTest,
// MarshallPlexiPowerAmpTest, OutputLoadTest, PickupLoadingTest,
// MarshallPlexi1959AmpTest); this checks the pedal around it:
//
//  1. the panel: eleven uniquely named knobs, defaults in range, selectors labelled;
//  2. the wiring: the pedal's output is EXACTLY the amp's output, mapped from the
//     knob values, divided by the 64V full scale and passed through the tanh
//     guard - so a wrong mapping, scale or order shows up as a difference;
//  3. every control does what it says (Channel, Input, Pickup, Impedance, the
//     Volumes, Presence);
//  4. the Cabinet's speaker reaches the amp through SpeakerLoadLink;
//  5. defaults are a usable level for a guitar-level input;
//  6. it stays finite and inside (-1, 1) at maximum settings, for any block size,
//     mono or stereo, and the two channels of a stereo pair match;
//  7. the sound doesn't depend on the host sample rate;
//  8. the Output trim: -12 dB at 70 / -40 dB at 0 / exactly unity at 100, the amp's waveform
//     only scaled (the tone and distortion untouched), identical at the default, no click.

#include "../Source/MarshallPlexi1959Pedal.h"
#include "../Source/SpeakerLoadLink.h"
#include "../Source/dsp/MarshallPlexi1959Amp.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <complex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    PedalParameter* param(MarshallPlexi1959Pedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    std::vector<float> sine(double f, double amplitude, double sr, int n)
    {
        std::vector<float> v(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            v[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * f * i / sr));
        return v;
    }

    // Lock-in amplitude at exactly f (Hann-windowed).
    double level(const std::vector<float>& x, double f, double sr)
    {
        std::complex<double> sum = 0.0;
        auto n = static_cast<double>(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1.0));
            sum += w * static_cast<double>(x[i]) * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(i) / sr);
        }
        return std::abs(sum) * 2.0 / (n * 0.5);
    }

    // Run the pedal on a mono sine; return the settled second half.
    std::vector<float> runPedal(MarshallPlexi1959Pedal& pedal, double f, double volts, double sr = 48000.0, int block = 512, double seconds = 0.45)
    {
        pedal.prepare(sr, block, 1);
        auto n = static_cast<int>(seconds * sr);
        auto in = sine(f, volts, sr, n);
        for (int start = 0; start < n; start += block)
        {
            auto count = std::min(block, n - start);
            float* ch = in.data() + start;
            pedal.process(&ch, 1, count);
        }
        return std::vector<float>(in.begin() + n / 2, in.end());
    }

    double gainDb(MarshallPlexi1959Pedal& pedal, double f, double volts, double sr = 48000.0)
    {
        auto out = runPedal(pedal, f, volts, sr);
        return 20.0 * std::log10(std::max(level(out, f, sr), 1.0e-12) / volts);
    }

    double peakOf(const std::vector<float>& v)
    {
        double p = 0.0;
        for (auto y : v) p = std::max(p, static_cast<double>(std::abs(y)));
        return p;
    }

    constexpr double tiny = 0.001;
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // --- 1. The panel. ---
    std::printf("=== The panel ===\n");
    {
        MarshallPlexi1959Pedal pedal;
        std::set<juce::String> names;
        bool inRange = true, labelled = true;
        for (auto* p : pedal.getParameters())
        {
            names.insert(p->name);
            inRange &= p->defaultValue >= p->range.getStart() && p->defaultValue <= p->range.getEnd();
            if (! p->valueLabels.empty())
                labelled &= static_cast<int>(p->valueLabels.size()) == static_cast<int>(p->range.getEnd() - p->range.getStart()) + 1;
        }
        std::printf("  %d knobs: ", static_cast<int>(pedal.getParameters().size()));
        for (auto* p : pedal.getParameters()) std::printf("%s  ", p->name.toRawUTF8());
        std::printf("\n");
        check(pedal.getParameters().size() == 11 && names.size() == 11, "eleven uniquely named knobs");
        check(inRange, "every default is inside its range");
        check(labelled, "every selector has one label per position");
        check(pedal.getName() == "Marshall 1959HW Plexi", "named for the amp");
    }
    std::printf("\n");

    // --- 2. The wiring: pedal == amp mapped from the knobs. ---
    std::printf("=== The pedal is the amp, mapped from the knobs ===\n");
    {
        MarshallPlexi1959Pedal pedal;
        param(pedal, "Channel")->set(1.0f);          // Normal
        param(pedal, "Input")->set(1.0f);            // Low
        param(pedal, "Pickup")->set(2.0f);           // Humbucker
        param(pedal, "Volume II")->set(70.0f);
        param(pedal, "Treble")->set(35.0f);
        param(pedal, "Middle")->set(80.0f);
        param(pedal, "Bass")->set(20.0f);
        param(pedal, "Presence")->set(65.0f);
        param(pedal, "Impedance")->set(1.0f);        // 8 ohm

        MarshallPlexi1959Amp amp(48000.0);
        amp.setRouting(MarshallPlexiPreamp::Routing::ChannelII);
        amp.setLowInput(true);
        amp.setPickup(MarshallPlexi1959Amp::Pickup::Humbucker);
        amp.setVolumeII(0.70f);
        amp.setTreble(0.35f); amp.setMiddle(0.80f); amp.setBass(0.20f); amp.setPresence(0.65f);
        amp.setImpedanceTap(8.0);

        pedal.prepare(48000.0, 512, 1);
        auto in = sine(330.0, 0.25, 48000.0, 9600);
        auto viaPedal = in, viaAmp = in;
        for (int start = 0; start < 9600; start += 512)
        {
            auto count = std::min(512, 9600 - start);
            float* ch = viaPedal.data() + start;
            pedal.process(&ch, 1, count);
        }
        amp.processBlock(viaAmp.data(), viaAmp.data(), 9600);
        double worst = 0.0;
        for (size_t i = 0; i < viaAmp.size(); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(viaPedal[i]) - std::tanh(static_cast<double>(viaAmp[i]) / 64.0)));
        std::printf("  worst difference between the pedal and tanh(amp / 64V): %.2e\n", worst);
        check(worst < 1.0e-5, "channel, jack, pickup, tone, Presence and tap all reach the amp as mapped, at the 64V scale");
    }
    std::printf("\n");

    // --- 3. The controls. ---
    std::printf("=== Controls ===\n");
    {
        auto make = [](std::initializer_list<std::pair<const char*, float>> values) {
            auto pedal = std::make_unique<MarshallPlexi1959Pedal>();
            for (auto& v : values) param(*pedal, v.first)->set(v.second);
            return pedal;
        };

        // Channel: High Treble is far brighter than Normal.
        auto tilt = [&](float ch) {
            auto p = make({ { "Channel", ch }, { "Volume I", 70.0f }, { "Volume II", 70.0f } });
            return gainDb(*p, 4000.0, tiny) - gainDb(*p, 100.0, tiny);
        };
        auto tiltI = tilt(0.0f), tiltII = tilt(1.0f);
        std::printf("  4kHz vs 100Hz: High Treble %+.1f dB, Normal %+.1f dB\n", tiltI, tiltII);
        check(tiltI > tiltII + 15.0, "Channel: High Treble is far brighter than Normal");

        // Input: Low is 6dB down (no pickup model selected).
        auto g = [&](float in) { auto p = make({ { "Channel", 1.0f }, { "Volume II", 70.0f }, { "Input", in } }); return gainDb(*p, 1000.0, tiny); };
        auto hi = g(0.0f), lo = g(1.0f);
        std::printf("  Low input against High: %+.2f dB\n", lo - hi);
        check(std::abs((lo - hi) + 6.02) < 0.4, "Input: the Low jack is 6dB down");

        // Pickup: only on the Low jack.
        auto pk = [&](float input, float kind) {
            auto p = make({ { "Channel", 1.0f }, { "Volume II", 70.0f }, { "Input", input }, { "Pickup", kind } });
            return gainDb(*p, 3000.0, tiny);
        };
        auto lowDelta = pk(1.0f, 2.0f) - pk(1.0f, 0.0f), highDelta = pk(0.0f, 2.0f) - pk(0.0f, 0.0f);
        std::printf("  a humbucker at 3kHz: %+.2f dB on the Low jack, %+.2f dB on the High jack\n", lowDelta, highDelta);
        check(lowDelta < -4.0 && std::abs(highDelta) < 0.3, "Pickup: it darkens the Low jack and leaves the High jack alone");

        // Impedance: 4 ohm on a matched 4 ohm cabinet swings half the voltage.
        auto swing = [&](float imp) {
            auto p = make({ { "Channel", 2.0f }, { "Volume I", 80.0f }, { "Volume II", 80.0f }, { "Impedance", imp } });
            return peakOf(runPedal(*p, 400.0, 0.6));
        };
        auto s16 = swing(2.0f), s4 = swing(0.0f);
        std::printf("  hard-driven output peak: %.3f (16 ohm) / %.3f (4 ohm), volts-equivalent %.1f / %.1f\n", s16, s4, 64.0 * std::atanh(std::min(s16, 0.9999)), 64.0 * std::atanh(std::min(s4, 0.9999)));
        check(s4 < 0.65 * s16 && s4 > 0.35 * s16, "Impedance: 4 ohms swings about half the voltage of 16 (the same power)");

        // Volumes and Presence.
        auto lvl = [&](const char* name, float v, float ch) {
            auto p = make({ { "Channel", ch }, { name, v } });
            if (std::string(name) == "Volume I") param(*p, "Volume II")->set(0.0f);
            if (std::string(name) == "Volume II") param(*p, "Volume I")->set(0.0f);
            auto out = runPedal(*p, 1000.0, tiny);
            return level(out, 1000.0, 48000.0);
        };
        auto ratioI = lvl("Volume I", 80.0f, 0.0f) / lvl("Volume I", 40.0f, 0.0f), ratioII = lvl("Volume II", 80.0f, 1.0f) / lvl("Volume II", 40.0f, 1.0f);
        std::printf("  Volume 40 -> 80 raises the level %.2fx (I, whose bright cap flattens the taper) and %.2fx (II)\n", ratioI, ratioII);
        check(ratioI > 1.3 && ratioII > 2.0, "Volume I and II each raise the level");
        auto top = [&](float presence) {
            auto p = make({ { "Channel", 1.0f }, { "Volume II", 70.0f }, { "Presence", presence } });
            return gainDb(*p, 4000.0, tiny) - gainDb(*p, 500.0, tiny);
        };
        auto t0 = top(0.0f), t1 = top(100.0f);
        std::printf("  4kHz vs 500Hz: Presence 0 %+.1f dB, Presence 100 %+.1f dB\n", t0, t1);
        check(t1 > t0 + 6.0, "Presence opens the top end");
    }
    std::printf("\n");

    // --- 4. The Cabinet's speaker. ---
    std::printf("=== The speaker link ===\n");
    {
        auto lowEnd = [&](bool publishSpeaker) {
            MarshallPlexi1959Pedal pedal;
            param(pedal, "Channel")->set(1.0f);
            param(pedal, "Volume II")->set(70.0f);
            param(pedal, "Presence")->set(100.0f);
            pedal.prepare(48000.0, 256, 1);
            AmpSpeakerLoad::Speaker sp;
            sp.fs = 200.0;
            auto measure = [&](double f) {
                auto n = static_cast<int>(0.4 * 48000.0);
                auto in = sine(f, tiny, 48000.0, n);
                for (int start = 0; start < n; start += 256)
                {
                    if (publishSpeaker)
                        SpeakerLoadLink::publish(sp);
                    auto count = std::min(256, n - start);
                    float* ch = in.data() + start;
                    pedal.process(&ch, 1, count);
                }
                return level(std::vector<float>(in.begin() + n / 2, in.end()), f, 48000.0);
            };
            return 20.0 * std::log10(measure(85.0) / measure(400.0));
        };
        auto generic = lowEnd(false), linked = lowEnd(true);
        std::printf("  85Hz vs 400Hz, Presence 100: %+.1f dB with the generic speaker (resonant at 85Hz), %+.1f dB with a Cabinet publishing a speaker resonant at 200Hz\n", generic, linked);
        check(generic > linked + 1.0, "a speaker published by the Cabinet replaces the generic one in the amp's feedback loop");
    }
    std::printf("\n");

    // --- 5. Default level. ---
    std::printf("=== Default level ===\n");
    {
        MarshallPlexi1959Pedal pedal;
        auto out = runPedal(pedal, 220.0, 0.2);
        auto peak = peakOf(out);
        std::printf("  a 0.2V guitar at 220Hz: output peak %.3f (full scale = 1)\n", peak);
        check(peak > 0.1 && peak <= 1.0, "the defaults give a healthy, non-clipping level for a guitar-level input");
    }
    std::printf("\n");

    // --- 6. Robustness. ---
    std::printf("=== Robustness ===\n");
    {
        MarshallPlexi1959Pedal pedal;
        for (auto* p : pedal.getParameters())
            p->set(p->name == "Impedance" ? 2.0f : p->range.getEnd());
        bool ok = true;
        for (int block : { 1, 7, 64, 512, 4096 })
        {
            pedal.prepare(48000.0, block, 2);
            juce::Random random(123);
            for (int rep = 0; rep < 6000 / std::max(block, 64) + 1; ++rep)
            {
                std::vector<float> left(static_cast<size_t>(block)), right(static_cast<size_t>(block));
                for (int i = 0; i < block; ++i) left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)] = 1.5f * (random.nextFloat() * 2.0f - 1.0f);
                float* channels[2] = { left.data(), right.data() };
                pedal.process(channels, 2, block);
                for (int i = 0; i < block; ++i)
                    ok &= std::isfinite(left[static_cast<size_t>(i)]) && std::abs(left[static_cast<size_t>(i)]) < 1.0f && left[static_cast<size_t>(i)] == right[static_cast<size_t>(i)];
            }
        }
        check(ok, "maximum knobs on 1.5V noise: finite, inside (-1, 1), for block sizes 1 to 4096, and both stereo channels match");

        // Stereo: identical channels (a duplicated mono guitar, processed once and
        // copied) and different channels (each processed on its own) must both give
        // exactly what separate mono runs give.
        {
            auto left = sine(220.0, 0.3, 48000.0, 9600), right = sine(330.0, 0.15, 48000.0, 9600);
            auto monoRun = [&](std::vector<float> x) {
                MarshallPlexi1959Pedal p;
                p.prepare(48000.0, 512, 1);
                for (int start = 0; start < 9600; start += 512)
                {
                    float* c = x.data() + start;
                    p.process(&c, 1, std::min(512, 9600 - start));
                }
                return x;
            };
            auto stereoRun = [&](std::vector<float> a, std::vector<float> b) {
                MarshallPlexi1959Pedal p;
                p.prepare(48000.0, 512, 2);
                for (int start = 0; start < 9600; start += 512)
                {
                    float* c[2] = { a.data() + start, b.data() + start };
                    p.process(c, 2, std::min(512, 9600 - start));
                }
                return std::make_pair(a, b);
            };
            auto refL = monoRun(left), refR = monoRun(right);
            auto same = stereoRun(left, left), diff = stereoRun(left, right);
            double dSame = 0.0, dDiff = 0.0;
            for (size_t i = 0; i < refL.size(); ++i)
            {
                dSame = std::max({ dSame, static_cast<double>(std::abs(same.first[i] - refL[i])), static_cast<double>(std::abs(same.second[i] - refL[i])) });
                dDiff = std::max({ dDiff, static_cast<double>(std::abs(diff.first[i] - refL[i])), static_cast<double>(std::abs(diff.second[i] - refR[i])) });
            }
            std::printf("  stereo vs separate mono runs: identical channels differ by %.1e, different channels by %.1e\n", dSame, dDiff);
            check(dSame < 1.0e-6 && dDiff < 1.0e-6, "identical stereo (processed once) and different stereo (processed twice) both equal the mono results");
        }

        MarshallPlexi1959Pedal mono;
        mono.prepare(48000.0, 128, 1);
        std::vector<float> data(128, 0.3f);
        float* ch = data.data();
        mono.process(&ch, 1, 128);
        check(true, "runs mono");
    }
    std::printf("\n");

    // --- 7. Host sample rate. ---
    std::printf("=== Sample-rate independence ===\n");
    {
        double lvl[3], tilt[3];
        int i = 0;
        for (auto sr : { 44100.0, 48000.0, 96000.0 })
        {
            MarshallPlexi1959Pedal p;
            param(p, "Channel")->set(1.0f);
            param(p, "Volume II")->set(70.0f);
            lvl[i] = gainDb(p, 1000.0, tiny, sr);
            tilt[i] = gainDb(p, 4000.0, tiny, sr) - gainDb(p, 100.0, tiny, sr);
            ++i;
        }
        std::printf("  1kHz gain %.2f / %.2f / %.2f dB, 4kHz-vs-100Hz tilt %.2f / %.2f / %.2f dB (44.1 / 48 / 96 kHz)\n", lvl[0], lvl[1], lvl[2], tilt[0], tilt[1], tilt[2]);
        check(std::abs(lvl[0] - lvl[1]) < 0.5 && std::abs(lvl[2] - lvl[1]) < 0.5, "1kHz gain agrees across sample rates (0.5dB)");
        check(std::abs(tilt[0] - tilt[1]) < 1.0 && std::abs(tilt[2] - tilt[1]) < 1.0, "spectral tilt agrees across sample rates (1dB)");
    }
    std::printf("\n");

    // --- 8. The Output trim. ---
    std::printf("=== Output trim ===\n");
    {
        // The hot case that motivated it: Jumped channels, Volumes up, a guitar-level input, so the amp
        // is hard into saturation and its output shape is far from a sine.
        auto hot = [](float outputKnob) {
            MarshallPlexi1959Pedal p;
            param(p, "Channel")->set(2.0f);
            param(p, "Volume I")->set(40.0f);
            param(p, "Volume II")->set(40.0f);
            param(p, "Output")->set(outputKnob);
            return runPedal(p, 330.0, 0.25);
        };
        auto full = hot(100.0f), trimmed = hot(70.0f), quiet = hot(0.0f);

        // The trim is applied ahead of the pedal's output tanh, so undoing the tanh recovers the amp's own
        // waveform scaled: atanh(out) must be EXACTLY (trim gain) x atanh(out at 0 dB). That is the whole
        // "doesn't change the tone" claim - the same waveform, so the same distortion, only smaller.
        auto expectedGain = MarshallPlexi1959Pedal::outputTrimGain(70.0f);
        double worstShape = 0.0, peakPre = 0.0;
        for (size_t i = 0; i < full.size(); ++i)
        {
            auto pre = std::atanh(std::min(0.999999, std::abs(static_cast<double>(full[i]))));
            peakPre = std::max(peakPre, pre);
            auto a = std::atanh(std::clamp(static_cast<double>(full[i]), -0.999999, 0.999999)) * expectedGain;
            auto b = std::atanh(std::clamp(static_cast<double>(trimmed[i]), -0.999999, 0.999999));
            worstShape = std::max(worstShape, std::abs(a - b));
        }
        std::printf("  Output 70 = %.2f dB (gain %.4f); worst waveform difference from the 0 dB waveform x that gain: %.2e (peak %.3f)\n",
                    20.0 * std::log10(expectedGain), expectedGain, worstShape, peakPre);
        check(std::abs(20.0 * std::log10(expectedGain) - (-12.0)) < 0.01, "Output 70 is -12 dB (0.4 dB per step)");
        check(MarshallPlexi1959Pedal::outputTrimGain(100.0f) == 1.0f, "Output 100 is exactly unity");
        check(std::abs(20.0 * std::log10(MarshallPlexi1959Pedal::outputTrimGain(0.0f)) - (-40.0)) < 0.01, "Output 0 is -40 dB");
        check(worstShape < 2.0e-3 * std::max(peakPre, 1.0), "trimmed, the amp's waveform is the same shape, just scaled (distortion unchanged)");
        check(peakOf(trimmed) < peakOf(full) * 0.5, "and it is much quieter (-12 dB takes the peak below half)");
        check(peakOf(quiet) < 0.02, "Output 0 is nearly silent");

        // At 0 dB the pedal is bit-for-bit what it was before the knob existed.
        MarshallPlexi1959Pedal p1, p2;
        param(p2, "Output")->set(100.0f);
        auto a = runPedal(p1, 330.0, 0.25), b2 = runPedal(p2, 330.0, 0.25);
        check(a == b2, "the default Output leaves the amp exactly as it was");
        check(param(p1, "Output")->defaultValue == 100.0f, "and 100 is its default (existing presets and sounds are unchanged)");

        // Turning the knob mid-signal doesn't click: jump 100 -> 60 at a block boundary and look at the worst
        // second difference of the output at the change against the signal's own elsewhere.
        for (auto changeTo : { 60.0f, 0.0f })
        {
            const int block = 512, total = 48000, changeAt = block * 50;
            MarshallPlexi1959Pedal p;
            param(p, "Channel")->set(1.0f);
            param(p, "Volume II")->set(30.0f);
            p.prepare(48000.0, block, 2);
            auto in = sine(196.0, 0.25, 48000.0, total);
            std::vector<float> out(static_cast<size_t>(total));
            for (int start = 0; start + block <= total; start += block)
            {
                if (start == changeAt)
                    param(p, "Output")->set(changeTo);
                std::vector<float> l(in.begin() + start, in.begin() + start + block), r = l;
                float* ch[2] = { l.data(), r.data() };
                p.process(ch, 2, block);
                if (std::memcmp(l.data(), r.data(), sizeof(float) * static_cast<size_t>(block)) != 0)
                    check(false, "left and right stay identical while the Output changes");
                std::copy(l.begin(), l.end(), out.begin() + start);
            }
            double natural = 0.0, atChange = 0.0;
            for (int i = 20000; i < total - 2; ++i)
            {
                auto d2 = std::abs(static_cast<double>(out[static_cast<size_t>(i) + 1]) - 2.0 * out[static_cast<size_t>(i)] + out[static_cast<size_t>(i) - 1]);
                if (std::abs(i - changeAt) <= 40) atChange = std::max(atChange, d2); else natural = std::max(natural, d2);
            }
            std::printf("  Output 100 -> %.0f at a block boundary: worst discontinuity x%.2f the signal's own\n", changeTo, atChange / natural);
            check(atChange < natural * 1.5, "turning Output does not click");
        }
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
