// Verify the Centaur Drive model's actual claims, taken from the circuit
// research it's built on (ElectroSmash / Coda Effects teardowns of the Klon
// Centaur): a ~1kHz mid-hump gain stage whose level rises with Gain, a
// germanium clipper (~0.35V forward drop, softer/lower than silicon), a clean
// path that recedes as Gain rises but never disappears, and a tone control
// that is a 408Hz high shelf spanning -8dB..+18.2dB. On top of that core, the
// series "Light Distortion" stage must be inert at 0 and must add real
// clipping as it rises, and Depth must be a low-frequency peak that leaves
// the rest of the spectrum alone.
//
// Linear-response checks (hump, Tone, Depth) run at a tiny amplitude so the
// clippers stay in their linear region, and compare the SAME frequency
// between two settings rather than raw magnitudes - a ratio isolates the one
// control under test and is immune to Goertzel scalloping (see
// GraphicEQTest for the earlier lesson).

#include "../Source/dsp/CentaurDriveStage.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 16384;
    constexpr int settleSamples = 4096; // skip filter/DC-blocker start-up
    // Measured: +4.3dB with the clean path, +1.6dB with it removed -> midpoint.
    constexpr double GROWTH_THRESHOLD_DB = 3.0;

    struct Settings
    {
        float drive = 0.5f;
        float lightDistortion = 0.0f;
        float depth = 0.0f;
        float tone = 0.3049f; // ~0dB: the shelf is flat here (see toneGainDb)
    };

    std::vector<float> sineAt(double freq, float amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }

    std::vector<float> render(const Settings& s, const std::vector<float>& input)
    {
        CentaurDriveStage stage(sampleRate);
        stage.setDrive(s.drive);
        stage.setLightDistortion(s.lightDistortion);
        stage.setDepth(s.depth);
        stage.setTone(s.tone);

        std::vector<float> output(input.size());
        stage.processBlock(input.data(), output.data(), static_cast<int>(input.size()));
        return std::vector<float>(output.begin() + settleSamples, output.end());
    }

    double magnitudeAt(const Settings& s, double freq, float amplitude)
    {
        return TestUtils::goertzelMagnitude(render(s, sineAt(freq, amplitude)), freq, sampleRate);
    }

    // Goertzel on a raw (rectangular-windowed) buffer leaks the fundamental
    // into every other bin at roughly -45dB, which reads as ~0.7% "THD" on a
    // perfectly clean signal - a Hann window drops that leakage far below the
    // distortion levels being measured here.
    std::vector<float> hannWindowed(std::vector<float> v)
    {
        for (size_t n = 0; n < v.size(); ++n)
            v[n] *= static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(v.size())));
        return v;
    }

    double thd(const Settings& s, double freq, float amplitude)
    {
        auto out = hannWindowed(render(s, sineAt(freq, amplitude)));
        auto fundamental = TestUtils::goertzelMagnitude(out, freq, sampleRate);
        double harmonics = 0.0;
        for (int h = 2; h <= 5; ++h)
        {
            auto m = TestUtils::goertzelMagnitude(out, freq * h, sampleRate);
            harmonics += m * m;
        }
        return std::sqrt(harmonics) / std::max(1.0e-9, fundamental);
    }

    double rms(const std::vector<float>& v)
    {
        double sum = 0.0;
        for (auto x : v)
            sum += static_cast<double>(x) * x;
        return std::sqrt(sum / static_cast<double>(v.size()));
    }

    double crestFactor(const std::vector<float>& v)
    {
        double peak = 0.0;
        for (auto x : v)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        return peak / std::max(1.0e-9, rms(v));
    }

    // Forward voltage of one diode of the antiparallel pair at a given
    // current: I = Is*(exp(V/Vt) - 1)  =>  V = Vt*ln(I/Is + 1)
    double forwardVoltage(const DiodeClipperStage::Parameters& p, double current)
    {
        return p.thermalVoltage * std::log(current / p.saturationCurrent + 1.0);
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: the diodes really are germanium-like, and softer than silicon ---
    std::printf("=== Diodes: germanium ~0.35V forward drop, well below silicon ===\n");
    {
        auto ge = forwardVoltage(CentaurDriveStage::germaniumDiodeParameters(), 1.0e-3);
        auto si = forwardVoltage(CentaurDriveStage::siliconDiodeParameters(), 1.0e-3);
        std::printf("  germanium Vf @ 1mA = %.3fV (source: ~0.35V)\n", ge);
        std::printf("  silicon   Vf @ 1mA = %.3fV (typical: ~0.6V)\n", si);

        bool ok = ge > 0.30 && ge < 0.40 && si > 0.52 && si < 0.68 && ge < si - 0.15;
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 2: Tone range matches the real circuit's -8dB..+18.24dB ---
    std::printf("=== Tone: real range -8dB..+18.24dB, 408Hz high shelf ===\n");
    {
        auto lo = CentaurDriveStage::toneGainDb(0.0f);
        auto hi = CentaurDriveStage::toneGainDb(1.0f);
        std::printf("  knob min = %.2fdB, knob max = %.2fdB\n", lo, hi);
        bool rangeOk = std::abs(lo - (-7.96)) < 0.1 && std::abs(hi - 18.24) < 0.1;

        Settings minTone, maxTone;
        minTone.drive = maxTone.drive = 0.0f;
        minTone.tone = 0.0f;
        maxTone.tone = 1.0f;

        constexpr float amp = 0.001f; // stays linear
        auto diffDb = [&](double f)
        { return TestUtils::toDb(magnitudeAt(maxTone, f, amp)) - TestUtils::toDb(magnitudeAt(minTone, f, amp)); };

        auto atBass = diffDb(60.0);
        auto atCorner = diffDb(408.0);
        auto atTreble = diffDb(8000.0);
        auto expectedSpan = hi - lo;
        std::printf("  max-vs-min Tone at   60Hz: %+.2fdB (shelf should not touch bass)\n", atBass);
        std::printf("  max-vs-min Tone at  408Hz: %+.2fdB (about half of %.1fdB at the corner)\n", atCorner, expectedSpan);
        std::printf("  max-vs-min Tone at 8000Hz: %+.2fdB (full %.1fdB span)\n", atTreble, expectedSpan);

        bool shapeOk = std::abs(atBass) < 1.0
                       && std::abs(atCorner - expectedSpan / 2.0) < 3.5
                       && std::abs(atTreble - expectedSpan) < 2.0;
        std::printf("  %s\n\n", (rangeOk && shapeOk) ? "OK" : "FAILED");
        allPassed &= rangeOk && shapeOk;
    }

    // --- Test 3: mid-hump, and Gain raises it ---
    std::printf("=== Gain stage: mid-hump around 1kHz that grows with Gain ===\n");
    {
        constexpr float amp = 0.001f;
        const double freqs[] = { 60, 100, 200, 400, 700, 1000, 1500, 2500, 4000, 8000 };

        auto sweep = [&](float drive)
        {
            Settings s;
            s.drive = drive;
            std::vector<double> mags;
            for (auto f : freqs)
                mags.push_back(TestUtils::toDb(magnitudeAt(s, f, amp)));
            return mags;
        };

        auto low = sweep(0.0f);
        auto high = sweep(1.0f);

        size_t peakIdx = 0;
        for (size_t i = 0; i < high.size(); ++i)
            if (high[i] > high[peakIdx]) peakIdx = i;

        std::printf("  Gain=1 response (dB):");
        for (size_t i = 0; i < high.size(); ++i)
            std::printf("  %.0fHz:%+.1f", freqs[i], high[i]);
        std::printf("\n  peak at %.0fHz\n", freqs[peakIdx]);

        auto peakHigh = high[peakIdx];
        size_t lowPeakIdx = 0;
        for (size_t i = 0; i < low.size(); ++i)
            if (low[i] > low[lowPeakIdx]) lowPeakIdx = i;
        auto peakLow = low[lowPeakIdx];
        std::printf("  peak level: Gain=0 %+.1fdB, Gain=1 %+.1fdB (%+.1fdB)\n", peakLow, peakHigh, peakHigh - peakLow);

        bool peakInMids = freqs[peakIdx] >= 500.0 && freqs[peakIdx] <= 1500.0;
        bool isHump = peakHigh - high[1] > 6.0 && peakHigh - high[high.size() - 1] > 6.0; // vs 100Hz and 8kHz
        bool gainRaisesHump = peakHigh - peakLow > 10.0;
        std::printf("  %s: peak lands in the mids | %s: real hump (>6dB over both ends) | %s: Gain raises it >10dB\n\n",
                    peakInMids ? "OK" : "FAILED", isHump ? "OK" : "FAILED", gainRaisesHump ? "OK" : "FAILED");
        allPassed &= peakInMids && isHump && gainRaisesHump;
    }

    // --- Test 4: Drive adds growing distortion ---
    std::printf("=== Drive: distortion grows with Gain ===\n");
    {
        Settings s;
        s.drive = 0.0f;
        auto d0 = thd(s, 220.0, 0.25f);
        s.drive = 0.5f;
        auto d5 = thd(s, 220.0, 0.25f);
        s.drive = 1.0f;
        auto d10 = thd(s, 220.0, 0.25f);
        std::printf("  Gain=0.0: THD = %.4f\n  Gain=0.5: THD = %.4f\n  Gain=1.0: THD = %.4f\n", d0, d5, d10);
        bool ok = d5 > d0 && d10 > d5 && d10 > 0.05;
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 5: touch sensitivity - distortion follows picking strength ---
    std::printf("=== Touch: harder picking distorts more at a fixed Gain ===\n");
    {
        Settings s;
        s.drive = 0.5f;
        auto soft = thd(s, 220.0, 0.05f);
        auto medium = thd(s, 220.0, 0.2f);
        auto hard = thd(s, 220.0, 0.6f);
        std::printf("  pick 0.05: THD = %.4f\n  pick 0.20: THD = %.4f\n  pick 0.60: THD = %.4f\n", soft, medium, hard);
        bool ok = medium > soft && hard > medium && soft < 0.05;
        std::printf("  %s: soft picking stays near clean, hard picking breaks up\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 6: the clean path survives even at max Gain ---
    std::printf("=== Clean path: some clean signal survives at max Gain ===\n");
    {
        // A pure clipper flattens: once it's driven far past its knee, more
        // input barely raises its fundamental. With clean signal still mixed
        // in, the output must keep growing. Driven 12dB harder (1.0 -> 4.0,
        // deep into saturation) a lone germanium clipper only creeps up
        // along its soft knee, so the growth that remains is the clean path.
        // (An earlier version of this check doubled a much smaller input
        // and passed even with the clean path removed - a mutation check
        // caught that it couldn't tell the difference.)
        Settings s;
        s.drive = 1.0f;
        auto quiet = magnitudeAt(s, 220.0, 1.0f);
        auto loud = magnitudeAt(s, 220.0, 4.0f);
        auto growthDb = TestUtils::toDb(loud) - TestUtils::toDb(quiet);
        std::printf("  input +12.0dB at Gain=1 (deep saturation): fundamental %+.2fdB\n", growthDb);
        bool ok = growthDb > GROWTH_THRESHOLD_DB;
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 7: Light Distortion is inert at 0 and continuous just above it ---
    std::printf("=== Light Distortion: bypassed at 0, no click just above it ===\n");
    {
        Settings s;
        s.drive = 0.0f;
        auto cleanThd = thd(s, 220.0, 0.05f);
        std::printf("  Drive=0, LightDist=0: THD at soft picking = %.4f (near clean)\n", cleanThd);

        Settings zero = s, tiny = s;
        zero.lightDistortion = 0.0f;
        tiny.lightDistortion = 0.001f;
        auto input = sineAt(330.0, 0.3f);
        auto a = render(zero, input);
        auto b = render(tiny, input);
        double maxDiff = 0.0, peak = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(a[i] - b[i])));
            peak = std::max(peak, static_cast<double>(std::abs(a[i])));
        }
        std::printf("  LightDist 0 vs 0.001: max difference = %.5f of peak %.4f\n", maxDiff, peak);

        bool ok = cleanThd < 0.03 && maxDiff < 0.02 * peak;
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 8: Light Distortion adds real hard clipping in series ---
    std::printf("=== Light Distortion: adds harmonics and flattens peaks as it rises ===\n");
    {
        Settings s;
        s.drive = 0.3f;

        double thdAt[3], crestAt[3], rmsAt[3];
        const float amounts[] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            s.lightDistortion = amounts[i];
            auto out = render(s, sineAt(220.0, 0.25f));
            thdAt[i] = thd(s, 220.0, 0.25f);
            crestAt[i] = crestFactor(out);
            rmsAt[i] = rms(out);
            std::printf("  LightDist=%.1f: THD = %.4f, crest = %.3f, RMS = %.4f\n", amounts[i], thdAt[i], crestAt[i], rmsAt[i]);
        }

        bool harmonicsGrow = thdAt[1] > thdAt[0] && thdAt[2] > thdAt[1];
        bool flattens = crestAt[2] < crestAt[0];
        auto levelSwingDb = TestUtils::toDb(rmsAt[2]) - TestUtils::toDb(rmsAt[0]);
        bool levelOk = std::abs(levelSwingDb) < 6.0;
        std::printf("  level swing 0 -> max: %+.1fdB\n", levelSwingDb);
        std::printf("  %s: harmonics grow | %s: peaks flatten | %s: loudness stays within +/-6dB\n\n",
                    harmonicsGrow ? "OK" : "FAILED", flattens ? "OK" : "FAILED", levelOk ? "OK" : "FAILED");
        allPassed &= harmonicsGrow && flattens && levelOk;
    }

    // --- Test 9: Depth is a low peak that leaves everything else alone ---
    std::printf("=== Depth: boost-only low-frequency peak ===\n");
    {
        Settings off, on;
        off.drive = on.drive = 0.0f;
        off.depth = 0.0f;
        on.depth = 1.0f;

        constexpr float amp = 0.001f;
        auto diffDb = [&](double f)
        { return TestUtils::toDb(magnitudeAt(on, f, amp)) - TestUtils::toDb(magnitudeAt(off, f, amp)); };

        auto atDepth = diffDb(95.0);
        auto atMid = diffDb(1000.0);
        auto atTreble = diffDb(5000.0);
        std::printf("  Depth max vs 0 at   95Hz: %+.2fdB (the peak)\n", atDepth);
        std::printf("  Depth max vs 0 at 1000Hz: %+.2fdB (should be untouched)\n", atMid);
        std::printf("  Depth max vs 0 at 5000Hz: %+.2fdB (should be untouched)\n", atTreble);

        bool ok = atDepth > 7.0 && atDepth < 12.0 && std::abs(atMid) < 1.0 && std::abs(atTreble) < 0.5;
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 10: default settings land near unity loudness ---
    std::printf("=== Level: default-ish settings stay near unity gain ===\n");
    {
        Settings s;
        s.drive = 0.4f;
        s.lightDistortion = 0.2f;
        s.depth = 0.3f;
        s.tone = 0.55f;
        bool ok = true;
        for (float amp : { 0.1f, 0.25f, 0.5f })
        {
            auto in = sineAt(330.0, amp);
            auto out = render(s, in);
            std::vector<float> inTail(in.begin() + settleSamples, in.end());
            auto gainDb = TestUtils::toDb(rms(out)) - TestUtils::toDb(rms(inTail));
            std::printf("  input peak %.2f: output level %+.1fdB re input\n", amp, gainDb);
            ok &= std::abs(gainDb) < 6.0;
        }
        std::printf("  %s\n\n", ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 11: clipping is oversampled (aliasing stays low) ---
    std::printf("=== Aliasing: folded-back harmonics stay far below the signal ===\n");
    {
        // 1604 whole cycles in the buffer -> a coherent frequency, so a
        // Hann-windowed Goertzel measures the alias bins without leakage.
        const double f0 = 1604.0 * sampleRate / numSamples;
        Settings s;
        s.drive = 1.0f;
        s.lightDistortion = 1.0f;

        auto out = render(s, sineAt(f0, 0.5f));
        for (size_t n = 0; n < out.size(); ++n)
            out[n] *= static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(out.size())));

        auto fundamental = TestUtils::goertzelMagnitude(out, f0, sampleRate);
        double worst = -200.0;
        for (int h = 6; h <= 12; ++h)
        {
            auto f = f0 * h;
            auto folded = std::abs(sampleRate * std::round(f / sampleRate) - f); // where it lands
            if (folded < 300.0 || folded > 23000.0)
                continue;
            auto db = TestUtils::toDb(TestUtils::goertzelMagnitude(out, folded, sampleRate)) - TestUtils::toDb(fundamental);
            worst = std::max(worst, db);
            std::printf("  harmonic %2d (%.0fHz) folds to %.0fHz: %+.1fdB re fundamental\n", h, f, folded, db);
        }
        bool ok = worst < -50.0;
        std::printf("  worst alias %+.1fdB - %s\n\n", worst, ok ? "OK" : "FAILED");
        allPassed &= ok;
    }

    // --- Test 12: stability at the extremes ---
    std::printf("=== Stability across the guitar range at extreme settings ===\n");
    {
        bool stable = true;
        Settings s;
        s.drive = 1.0f;
        s.lightDistortion = 1.0f;
        s.depth = 1.0f;
        s.tone = 1.0f;
        for (double freq = 82.0; freq <= 5000.0; freq *= 1.7)
        {
            auto out = render(s, sineAt(freq, 0.95f));
            for (auto y : out)
            {
                if (!std::isfinite(y) || std::abs(y) > 20.0f)
                {
                    std::printf("  FAILED at %.1fHz\n", freq);
                    stable = false;
                    break;
                }
            }
        }
        std::printf("  %s\n\n", stable ? "OK: stable across the range" : "see failures above");
        allPassed &= stable;
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
