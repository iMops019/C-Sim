// End-to-end check of the real Super-Sonic 22 pedal (JUCE-coupled): the preamp
// is tested against the service diagram on its own (SuperSonic22PreampTest);
// this checks that the pedal wires preamp -> reverb -> power stage correctly.
//
//  1. the panel: twelve uniquely-named knobs with defaults in range;
//  2. the chain is what it claims: at a small signal, the pedal's output
//     equals the preamp's own output x the mixer gain (2.4) through the power
//     stage alone - each measured separately, so a wrong gain, a missing
//     stage or a wrong order shows up;
//  3. the switches and knobs do what they say (Burn is far more distorted
//     than Vintage; Fat is hotter than Normal by about the diagram's +6.7dB;
//     both Volumes are monotonic; Reverb leaves a tail and 0 leaves none);
//  4. defaults are usable levels for a guitar-level input;
//  5. it stays finite and inside (-1, 1) at maximum settings, for any block
//     size, mono or stereo, and both channels of a stereo pair match;
//  6. the sound doesn't depend on the host sample rate.
//
// Levels are measured at EXACTLY the test frequency over a whole number of
// cycles (see MesaLoadTest / CabSpeakerPushTest for why not the Goertzel helper).

#include "../Source/SuperSonic22Pedal.h"
#include "../Source/dsp/PowerAmpStage.h"
#include "../Source/dsp/SuperSonic22Preamp.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <complex>
#include <cstdio>
#include <set>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    PedalParameter* param(SuperSonic22Pedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    void setAll(SuperSonic22Pedal& p, std::initializer_list<std::pair<const char*, float>> values)
    {
        for (auto& v : values)
            param(p, v.first)->set(v.second);
    }

    // Magnitude at exactly freq over the given samples (whole cycles assumed).
    double magnitudeAt(const std::vector<float>& x, double freq, double sampleRate)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return std::abs(sum) * 2.0 / static_cast<double>(x.size());
    }

    std::vector<float> hann(const std::vector<float>& v)
    {
        std::vector<float> w(v.size());
        for (size_t i = 0; i < v.size(); ++i)
            w[i] = v[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(v.size() - 1)));
        return w;
    }

    // (H2..H7) / fundamental on a Hann-windowed copy.
    double harmonicRatio(const std::vector<float>& out, double freq, double sampleRate)
    {
        auto w = hann(out);
        double sum = 0.0;
        for (int h = 2; h <= 7; ++h)
        {
            auto m = magnitudeAt(w, freq * h, sampleRate);
            sum += m * m;
        }
        return std::sqrt(sum) / std::max(1.0e-12, magnitudeAt(w, freq, sampleRate));
    }

    double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

    struct Result
    {
        std::vector<float> left, right; // the last `keep` samples of each
        bool finite = true;
        float peak = 0.0f;
    };

    // Drive a (prepared, configured) pedal with a stereo-identical sine for
    // `seconds`, in blocks of `block`, and keep the last whole number of
    // cycles' worth (`keepCycles`) of both channels.
    Result runSine(SuperSonic22Pedal& pedal, double freq, double amplitude, double sampleRate, double seconds,
                   int keepCycles, int block = 512, int numChannels = 2)
    {
        auto total = static_cast<long>(sampleRate * seconds);
        auto keep = static_cast<long>(std::llround(keepCycles * sampleRate / freq));
        Result r;
        std::vector<float> l(static_cast<size_t>(block)), rr(static_cast<size_t>(block));
        for (long index = 0; index < total;)
        {
            auto n = static_cast<int>(std::min<long>(block, total - index));
            for (int i = 0; i < n; ++i)
                l[static_cast<size_t>(i)] = rr[static_cast<size_t>(i)] =
                    static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * static_cast<double>(index + i) / sampleRate));
            float* channels[2] = { l.data(), rr.data() };
            pedal.process(channels, numChannels, n);
            for (int i = 0; i < n; ++i)
            {
                auto v = l[static_cast<size_t>(i)];
                r.finite &= std::isfinite(v) && std::isfinite(rr[static_cast<size_t>(i)]);
                r.peak = std::max(r.peak, std::abs(v));
                if (index + i >= total - keep)
                {
                    r.left.push_back(v);
                    r.right.push_back(rr[static_cast<size_t>(i)]);
                }
            }
            index += n;
        }
        return r;
    }

    // A fresh pedal with the reverb off and the given settings.
    SuperSonic22Pedal* fresh(std::unique_ptr<SuperSonic22Pedal>& holder, double sampleRate,
                             std::initializer_list<std::pair<const char*, float>> values)
    {
        holder = std::make_unique<SuperSonic22Pedal>();
        holder->prepare(sampleRate, 512, 2);
        param(*holder, "Reverb")->set(0.0f);
        setAll(*holder, values);
        return holder.get();
    }

    constexpr double sr = 48000.0;
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };
    std::unique_ptr<SuperSonic22Pedal> holder;

    // --- 1. The panel. ---
    std::printf("=== The panel ===\n");
    {
        SuperSonic22Pedal pedal;
        auto params = pedal.getParameters();
        std::set<juce::String> names;
        bool inRange = true;
        for (auto* p : params)
        {
            names.insert(p->name);
            inRange &= p->defaultValue >= p->range.getStart() && p->defaultValue <= p->range.getEnd();
        }
        std::printf("  %zu knobs:", params.size());
        for (auto* p : params) std::printf(" [%s]", p->name.toRawUTF8());
        std::printf("\n");
        check(params.size() == 12 && names.size() == 12, "twelve knobs, all uniquely named (a preset stores them by name)");
        check(inRange, "every default is inside its range");
        check(pedal.getName() == "Fender Super-Sonic 22", "the name is the catalog's key");
        for (auto* wanted : { "Channel", "Fat", "Vintage Volume", "Vintage Treble", "Vintage Bass", "Gain 1", "Gain 2",
                              "Burn Treble", "Burn Bass", "Burn Mid", "Burn Volume", "Reverb" })
            if (param(pedal, wanted) == nullptr) { check(false, wanted); }
        std::printf("\n");
    }

    // --- 2. The chain is preamp x mixer gain x power stage. ---
    std::printf("=== The chain: pedal output = preamp -> x2.4 -> power stage (small signal, Reverb off) ===\n");
    for (bool burn : { false, true })
    {
        constexpr double freq = 1000.0, amplitude = 0.005; // a small signal: 5mV peak
        constexpr int cycles = 200;

        // The preamp alone, in volts, at the same knob positions the pedal defaults to.
        SuperSonic22Preamp preamp(sr);
        preamp.setChannel(burn ? SuperSonic22Preamp::Channel::Burn : SuperSonic22Preamp::Channel::Vintage);
        preamp.setVintageVolume(0.5f); preamp.setVintageTreble(0.5f); preamp.setVintageBass(0.5f);
        preamp.setGain1(0.35f); preamp.setGain2(0.5f);
        preamp.setBurnTreble(0.5f); preamp.setBurnBass(0.5f); preamp.setBurnMid(0.5f); preamp.setBurnVolume(0.4f);
        auto samples = static_cast<int>(std::llround(cycles * sr / freq));
        std::vector<float> in(static_cast<size_t>(samples)), volts(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i)
            in[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * i / sr));
        preamp.processBlock(in.data(), volts.data(), samples);

        // ...then through the mixer gain and a power stage alone.
        std::vector<float> stageIn(volts.size()), stageOut(volts.size());
        for (size_t i = 0; i < volts.size(); ++i)
            stageIn[i] = volts[i] * (210.0f / 88.0f) / 4.1f;
        PowerAmpStage power(sr);
        power.setSag(0.10f); power.setFeedback(0.15f); power.setDrive(0.0f);
        power.processBlock(stageIn.data(), stageOut.data(), samples);

        auto tail = [&](const std::vector<float>& v) { return std::vector<float>(v.end() - static_cast<long>(std::llround(100 * sr / freq)), v.end()); };
        auto expected = magnitudeAt(tail(stageOut), freq, sr);

        auto* pedal = fresh(holder, sr, { { "Channel", burn ? 1.0f : 0.0f } });
        auto r = runSine(*pedal, freq, amplitude, sr, static_cast<double>(cycles) / freq, 100);
        auto measured = magnitudeAt(r.left, freq, sr);

        std::printf("  %s: pedal %.5f, preamp -> x2.4 -> power stage %.5f  (%+.2f dB)\n", burn ? "Burn   " : "Vintage", measured, expected, db(measured / expected));
        check(std::abs(db(measured / expected)) < 0.5, "matches the separately-measured chain to within 0.5dB");
    }
    std::printf("\n");

    // --- 3. Switches and knobs. ---
    std::printf("=== Switches and knobs ===\n");
    {
        // Burn vs Vintage at the same guitar-ish input: Burn is far more distorted.
        auto distortion = [&](float channelValue)
        {
            auto* pedal = fresh(holder, sr, { { "Channel", channelValue }, { "Gain 1", 60.0f }, { "Gain 2", 100.0f } });
            auto r = runSine(*pedal, 220.0, 0.05, sr, 1.0, 100);
            return harmonicRatio(r.left, 220.0, sr);
        };
        auto vintageH = distortion(0.0f), burnH = distortion(1.0f);
        std::printf("  harmonics/fundamental at 50mV peak: Vintage %.3f, Burn (Gain 1 60, Gain 2 100) %.3f\n", vintageH, burnH);
        check(burnH > 4.0 * vintageH && burnH > 0.1, "Burn is at least 4x more distorted than Vintage, and heavily so");

        // Fat vs Normal, small signal: the diagram prints +6.7dB.
        auto level = [&](float fatValue)
        {
            auto* pedal = fresh(holder, sr, { { "Fat", fatValue } });
            auto r = runSine(*pedal, 1000.0, 0.005, sr, 0.5, 100);
            return magnitudeAt(r.left, 1000.0, sr);
        };
        auto fatDb = db(level(1.0f) / level(0.0f));
        std::printf("  Fat minus Normal at 5mV: %+.1f dB (the diagram prints +6.7 dB)\n", fatDb);
        check(fatDb > 4.5 && fatDb < 8.5, "Fat is hotter than Normal by about what the diagram says");

        // Both Volumes, monotonic.
        for (bool burn : { false, true })
        {
            std::printf("  %s Volume 0/25/50/75/100:", burn ? "Burn   " : "Vintage");
            double prev = -1.0;
            bool mono = true;
            double first = 0.0, last = 0.0;
            for (float v : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                auto* pedal = fresh(holder, sr, { { "Channel", burn ? 1.0f : 0.0f }, { "Gain 1", 15.0f }, { "Gain 2", 30.0f },
                                                   { burn ? "Burn Volume" : "Vintage Volume", v } });
                auto r = runSine(*pedal, 1000.0, 0.005, sr, 0.5, 100);
                auto m = magnitudeAt(r.left, 1000.0, sr);
                std::printf(" %.1fdB", db(m));
                mono &= m > prev;
                prev = m;
                if (v == 0.0f) first = m;
                last = m;
            }
            std::printf("\n");
            check(mono && db(last / first) > 30.0, "monotonic, with 30dB+ of range");
        }

        // Reverb: a tail after the input stops; none at 0.
        auto tailEnergy = [&](float reverbValue)
        {
            SuperSonic22Pedal pedal;
            pedal.prepare(sr, 512, 2);
            param(pedal, "Reverb")->set(reverbValue);
            std::vector<float> l(512), r(512);
            double energy = 0.0;
            for (int block = 0; block < 120; ++block)
            {
                for (int i = 0; i < 512; ++i)
                    l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                        block < 20 ? static_cast<float>(0.1 * std::sin(2.0 * M_PI * 330.0 * (block * 512 + i) / sr)) : 0.0f;
                float* channels[2] = { l.data(), r.data() };
                pedal.process(channels, 2, 512);
                if (block >= 40) // 0.43 s after the burst ended
                    for (auto v : l) energy += static_cast<double>(v) * v;
            }
            return energy;
        };
        auto dry = tailEnergy(0.0f), wet = tailEnergy(80.0f);
        std::printf("  energy 0.4-1.2s after a burst: Reverb 0 = %.2e, Reverb 80 = %.2e\n", dry, wet);
        check(wet > 20.0 * std::max(dry, 1.0e-12), "Reverb 80 leaves a tail that Reverb 0 does not (20x more energy)");
        std::printf("\n");
    }

    // --- 4. Defaults are usable levels. ---
    std::printf("=== Default levels for a guitar-level input (0.2 peak, 220Hz) ===\n");
    for (bool burn : { false, true })
    {
        SuperSonic22Pedal pedal;
        pedal.prepare(sr, 512, 2);
        param(pedal, "Channel")->set(burn ? 1.0f : 0.0f);
        auto r = runSine(pedal, 220.0, 0.2, sr, 1.0, 100);
        std::printf("  %s: peak %.2f\n", burn ? "Burn   " : "Vintage", r.peak);
        check(r.finite && r.peak > 0.1 && r.peak < 1.0, "audible and inside full scale at the default knob positions");
    }
    std::printf("\n");

    // --- 5. Robustness. ---
    std::printf("=== Robustness ===\n");
    {
        bool ok = true;
        float worst = 0.0f;
        for (float channelValue : { 0.0f, 1.0f })
            for (float fatValue : { 0.0f, 1.0f })
                for (int block : { 32, 512, 4096 })
                {
                    auto* pedal = fresh(holder, sr, { { "Channel", channelValue }, { "Fat", fatValue }, { "Vintage Volume", 100.0f },
                                                       { "Vintage Treble", 100.0f }, { "Vintage Bass", 100.0f }, { "Gain 1", 100.0f },
                                                       { "Gain 2", 100.0f }, { "Burn Treble", 100.0f }, { "Burn Bass", 100.0f },
                                                       { "Burn Mid", 100.0f }, { "Burn Volume", 100.0f }, { "Reverb", 100.0f } });
                    auto r = runSine(*pedal, 110.0, 1.0, sr, 0.5, 10, block);
                    ok &= r.finite && r.peak <= 1.0f;
                    worst = std::max(worst, r.peak);
                }
        std::printf("  worst peak at every knob at maximum, a 1.0-peak input, 3 block sizes: %.3f\n", worst);
        check(ok, "finite and inside full scale at maximum settings, both channels, Normal and Fat");

        auto* pedal = fresh(holder, sr, { { "Channel", 1.0f }, { "Gain 1", 80.0f } });
        auto r = runSine(*pedal, 220.0, 0.2, sr, 0.5, 50);
        bool same = r.left == r.right;
        check(same, "a stereo pair fed the same signal comes out identical on both channels");

        auto* mono = fresh(holder, sr, { { "Channel", 1.0f } });
        auto rm = runSine(*mono, 220.0, 0.2, sr, 0.3, 30, 512, 1);
        check(rm.finite, "processing one channel of a two-channel pedal is fine");
        std::printf("\n");
    }

    // --- 6. Sample-rate independence. ---
    std::printf("=== Sample-rate independence (small signal, 1kHz) ===\n");
    for (bool burn : { false, true })
    {
        double levels[3];
        int i = 0;
        for (auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto* pedal = fresh(holder, rate, { { "Channel", burn ? 1.0f : 0.0f } });
            auto r = runSine(*pedal, 1000.0, 0.005, rate, 0.5, 100);
            levels[i++] = db(magnitudeAt(r.left, 1000.0, rate));
        }
        std::printf("  %s: %.2f / %.2f / %.2f dB (44.1 / 48 / 96 kHz)\n", burn ? "Burn   " : "Vintage", levels[0], levels[1], levels[2]);
        check(std::abs(levels[0] - levels[1]) < 0.5 && std::abs(levels[2] - levels[1]) < 0.5, "output level agrees across sample rates (0.5dB)");
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
