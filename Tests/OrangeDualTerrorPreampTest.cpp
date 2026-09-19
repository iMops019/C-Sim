// Verify OrangeDualTerrorPreamp does what its header claims, checking the
// things that actually define the Dual Terror's two channels:
//  - the Tiny Terror's tiny 1nF coupling cap strips bass BEFORE the gain
//    stages (tight/punchy), while the Fat channel's 68nF caps and
//    direct-coupled cathode follower keep it (measured, small-signal);
//  - Gain drives real overdrive in both channels - upper harmonics grow
//    and the waveform compresses (crest factor falls);
//  - the Tone control's first-order shelf lands on the exact zero/pole
//    frequencies derived from the schematic's own component values, and
//    sweeps monotonically darker;
//  - the OR60 Bright switch orders shimmer < ... < bite in treble;
//  - the Gain pot's 100pF bright cap fades as Gain rises;
//  - Volume is linear, the cascade is stable at max settings, quiet input
//    stays audible, and the two channels are level-matched at default.
//
// Test-design note carried over from this project's other preamp tests:
// compare gain-independent metrics (ratios, crest factor), never raw
// magnitudes between differently-calibrated stages.

#include "../Source/dsp/OrangeDualTerrorPreamp.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    using Ch = OrangeDualTerrorPreamp::Channel;

    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 16384;

    struct Settings
    {
        Ch channel = Ch::TinyTerror;
        float gain = 0.5f, tone = 0.6f, volume = 1.0f;
        int bright = 1;
    };

    // Returns the settled second half of the output.
    std::vector<float> run(const Settings& s, double freq, double amplitude)
    {
        OrangeDualTerrorPreamp amp(sampleRate);
        amp.setChannel(s.channel);
        amp.setGain(s.gain);
        amp.setTone(s.tone);
        amp.setVolume(s.volume);
        amp.setBright(s.bright);

        std::vector<float> in(numSamples), out(numSamples);
        for (int n = 0; n < numSamples; ++n)
            in[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
        amp.processBlock(in.data(), out.data(), numSamples);
        return std::vector<float>(out.begin() + numSamples / 2, out.end());
    }

    double mag(const std::vector<float>& v, double f) { return TestUtils::goertzelMagnitude(v, f, sampleRate); }

    double dbRatio(const Settings& s, double freqHi, double freqLo, double amplitude = 0.02)
    {
        return TestUtils::toDb(mag(run(s, freqHi, amplitude), freqHi) / mag(run(s, freqLo, amplitude), freqLo));
    }

    double crest(const std::vector<float>& v)
    {
        double peak = 0.0, sumSq = 0.0;
        for (auto y : v) { peak = std::max(peak, static_cast<double>(std::abs(y))); sumSq += static_cast<double>(y) * y; }
        return peak / std::sqrt(sumSq / static_cast<double>(v.size()));
    }

    const char* name(Ch c) { return c == Ch::TinyTerror ? "Tiny Terror" : "Fat"; }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // --- 1. The defining contrast: Tiny Terror strips bass before the gain
    // stages, Fat keeps it. Small-signal (0.02 amp, Gain 0) so this
    // measures the linear filtering, not distortion. Measured: about
    // -10dB vs about +2dB (100Hz relative to 1kHz). ---
    std::printf("=== Low end: Tiny Terror is tight (1nF coupling), Fat is full (68nF) ===\n");
    {
        Settings tiny; tiny.channel = Ch::TinyTerror; tiny.gain = 0.0f;
        Settings fat;  fat.channel = Ch::Fat;         fat.gain = 0.0f;
        auto tinyBass = dbRatio(tiny, 100.0, 1000.0);
        auto fatBass = dbRatio(fat, 100.0, 1000.0);
        std::printf("  100Hz relative to 1kHz: Tiny Terror = %+.1f dB, Fat = %+.1f dB\n", tinyBass, fatBass);
        check(tinyBass < -5.0, "Tiny Terror cuts the bass");
        check(fatBass > tinyBass + 8.0, "Fat has at least 8dB more low end than Tiny Terror");
        std::printf("\n");
    }

    // --- 2 & 3. Gain must drive genuine overdrive in both channels. ---
    for (auto ch : { Ch::TinyTerror, Ch::Fat })
    {
        std::printf("=== Overdrive (%s): Gain adds upper harmonics and compresses ===\n", name(ch));
        auto harmonics = [ch](float g)
        {
            Settings s; s.channel = ch; s.gain = g;
            auto out = run(s, 220.0, 0.3);
            auto f = mag(out, 220.0);
            return (mag(out, 660.0) + mag(out, 1100.0) + mag(out, 1540.0)) / std::max(1.0e-9, f);
        };
        auto crestAt = [ch](float g) { Settings s; s.channel = ch; s.gain = g; return crest(run(s, 220.0, 0.3)); };

        auto lowH = harmonics(0.0f), highH = harmonics(1.0f);
        auto lowCrest = crestAt(0.25f), highCrest = crestAt(1.0f);
        std::printf("  (H3+H5+H7)/fund: Gain=0 %.3f, Gain=1 %.3f | crest: Gain=.25 %.2f, Gain=1 %.2f\n",
                     lowH, highH, lowCrest, highCrest);
        check(highH > lowH * 1.5, "odd-harmonic content grows with Gain");
        check(highCrest < lowCrest - 0.2 && highCrest < 1.4, "waveform is genuinely clipped/compressed at high Gain");
        std::printf("\n");
    }

    // --- 4. Tone: exact schematic-derived corners + monotonic sweep. ---
    std::printf("=== Tone: first-order shelf lands on the schematic-derived corners ===\n");
    {
        // Tiny Terror: 2.2nF, 500k pot, plate sources 2x(100k||62.5k).
        // Tone=1 (full pot in series): zero 1/(2pi*500k*2.2nF) = 144.7Hz,
        // pole 1/(2pi*(500k+76.9k)*2.2nF) = 125.4Hz. Tone=0 (pot shorted):
        // pure pole 1/(2pi*76.9k*2.2nF) = 940.7Hz.
        OrangeDualTerrorPreamp tiny(sampleRate);
        tiny.setChannel(Ch::TinyTerror);
        tiny.setTone(1.0f);
        auto z1 = tiny.getToneZeroHz(), p1 = tiny.getTonePoleHz();
        tiny.setTone(0.0f);
        auto p0 = tiny.getTonePoleHz();
        std::printf("  Tiny Terror: Tone=1 zero %.1fHz pole %.1fHz | Tone=0 pole %.1fHz\n", z1, p1, p0);
        check(std::abs(z1 - 144.7f) < 2.0f && std::abs(p1 - 125.4f) < 2.0f && std::abs(p0 - 940.7f) < 8.0f,
              "Tiny Terror corners match 2.2nF / 500k / 100k plate loads");

        // Fat: 4.7nF, plate loads 82k/100k: Tone=0 pole 1/(2pi*73.9k*4.7nF) = 458Hz.
        OrangeDualTerrorPreamp fat(sampleRate);
        fat.setChannel(Ch::Fat);
        fat.setTone(0.0f);
        auto fatP0 = fat.getTonePoleHz();
        std::printf("  Fat: Tone=0 pole %.1fHz\n", fatP0);
        check(std::abs(fatP0 - 458.0f) < 6.0f, "Fat corner matches 4.7nF / 82k+100k plate loads");

        for (auto ch : { Ch::TinyTerror, Ch::Fat })
        {
            double previous = -1.0e9;
            bool monotonic = true;
            std::printf("  %s: 3kHz vs 300Hz by Tone:", name(ch));
            for (float t : { 0.0f, 0.3f, 0.6f, 1.0f })
            {
                Settings s; s.channel = ch; s.gain = 0.0f; s.tone = t;
                auto r = dbRatio(s, 3000.0, 300.0);
                std::printf(" %+.1f", r);
                monotonic &= r > previous;
                previous = r;
            }
            std::printf(" dB\n");
            check(monotonic, "higher Tone is brighter");
        }
        std::printf("\n");
    }

    // --- 5. OR60 Bright switch. ---
    std::printf("=== Bright switch: shimmer and bite both brighten, bite most ===\n");
    for (auto ch : { Ch::TinyTerror, Ch::Fat })
    {
        double r[3];
        for (int b = 0; b < 3; ++b)
        {
            Settings s; s.channel = ch; s.gain = 0.0f; s.bright = b;
            r[b] = dbRatio(s, 4000.0, 400.0);
        }
        std::printf("  %s: 4kHz vs 400Hz: shimmer %+.1f, neutral %+.1f, bite %+.1f dB\n", name(ch), r[0], r[1], r[2]);
        check(r[0] > r[1] + 0.5 && r[2] > r[0] + 0.5, "neutral < shimmer < bite");
    }
    std::printf("\n");

    // --- 6. The Tiny Terror's Gain-pot bright cap fades as Gain rises. ---
    std::printf("=== Gain pot's 100pF bright cap: strongest at low Gain ===\n");
    {
        OrangeDualTerrorPreamp amp(sampleRate);
        amp.setGain(0.0f);
        auto low = amp.getPotBrightAmount();
        amp.setGain(1.0f);
        auto high = amp.getPotBrightAmount();
        std::printf("  bright amount at Gain=0: %.3f, at Gain=1: %.3f\n", low, high);
        check(low > high + 0.2f && high >= 0.0f, "bright cap fades as Gain rises");
        std::printf("\n");
    }

    // --- 7. Volume is linear (it sits after everything nonlinear). ---
    std::printf("=== Volume: output level scales linearly ===\n");
    for (auto ch : { Ch::TinyTerror, Ch::Fat })
    {
        Settings full; full.channel = ch; full.gain = 0.0f; full.volume = 1.0f;
        Settings quarter = full; quarter.volume = 0.25f;
        auto ratio = mag(run(full, 220.0, 0.3), 220.0) / std::max(1.0e-9, mag(run(quarter, 220.0, 0.3), 220.0));
        std::printf("  %s: Volume 1.0 / 0.25 = %.3f (expect ~4.0)\n", name(ch), ratio);
        check(ratio > 3.5 && ratio < 4.5, "Volume scales linearly");
    }
    std::printf("\n");

    // --- 8. Stability at maximum settings across a guitar-range sweep. ---
    std::printf("=== Stability at max settings (both channels, Bite, Tone 1, Volume 1) ===\n");
    {
        bool stable = true;
        for (auto ch : { Ch::TinyTerror, Ch::Fat })
            for (double f = 80.0; f <= 1200.0; f += 80.0)
            {
                Settings s; s.channel = ch; s.gain = 1.0f; s.tone = 1.0f; s.volume = 1.0f; s.bright = 2;
                for (auto y : run(s, f, 0.9))
                    if (! std::isfinite(y) || std::abs(y) > 5.0f) { stable = false; break; }
            }
        check(stable, "finite and bounded across the whole sweep");
        std::printf("\n");
    }

    // --- 9. A realistic quiet guitar is audible at default settings. ---
    std::printf("=== Regression: quiet input at default settings is audible ===\n");
    for (auto ch : { Ch::TinyTerror, Ch::Fat })
    {
        Settings s; s.channel = ch; s.volume = 0.5f;
        auto out = run(s, 110.0, 0.15);
        double peak = 0.0;
        for (auto y : out) peak = std::max(peak, static_cast<double>(std::abs(y)));
        std::printf("  %s: 0.15 in -> peak %.3f\n", name(ch), peak);
        check(peak > 0.1, "not crushed to near-silence");
    }
    std::printf("\n");

    // --- 10. The two channels are level-matched at default settings. ---
    std::printf("=== Level match: channels within 2dB of each other at default ===\n");
    {
        auto avgDb = [](Ch ch)
        {
            double sum = 0.0;
            for (double f : { 110.0, 220.0, 440.0, 880.0, 1760.0 })
            {
                Settings s; s.channel = ch; s.volume = 0.5f;
                auto out = run(s, f, 0.3);
                double sumSq = 0.0;
                for (auto y : out) sumSq += static_cast<double>(y) * y;
                sum += 10.0 * std::log10(sumSq / static_cast<double>(out.size()) / (0.3 * 0.3 / 2.0));
            }
            return sum / 5.0;
        };
        auto tiny = avgDb(Ch::TinyTerror), fat = avgDb(Ch::Fat);
        std::printf("  Tiny Terror %+.1f dB, Fat %+.1f dB\n", tiny, fat);
        check(std::abs(tiny - fat) < 2.0, "default levels match");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
