// Verify MarshallPlexiPreamp - the 1959HW's preamp, traced from the July 1970
// Unicord drawing of the Marshall 1959 and checked against Marshall's 1959HW
// owner's manual.
//
// That drawing prints no test voltages, so unlike the Super-Sonic 22 there is
// no printed number to calibrate against. The checks instead are:
//
//  - the DC operating points are self-consistent (the solver's answer obeys
//    Ohm's law and the tube curve) and roughly agree with the supply
//    droppers the c.1967 drawing labels (reported, not tuned);
//  - the small-signal response from the jack to V2-A's grid matches a frequency-
//    domain hand netlist of the drawn mixer - typed again here, separately from
//    the code, and solved with a different method - at several volume settings,
//    for both channels: the coupling caps, bright cap, 470k / 500pF, Miller
//    capacitance and the cathode shelf, all at once;
//  - V2-A's extra cathode bypass (the manual's "Tonal Note 1") does what the
//    manual says: it lifts the upper mids and top against the bass;
//  - the manual's statements about the jacks (Low is 6dB down) and the two
//    channels (High Treble is bright and thin, Normal full and dark);
//  - the behaviours that define the amp, each measured against something
//    gain-independent: distortion rises with level and with Volume, and is
//    mostly even-order because the cathode follower limits one half of the
//    swing; the gain compresses when driven;
//  - the usual: monotonic Volumes, superposition through the mixer, no blow-ups,
//    and the host sample rate does not change the sound.
//
// Goertzel readings are always compared against the same measurement of the
// input sine (see TestUtils.h - scalloping loss makes raw readings low).

#include "../Source/dsp/MarshallPlexiPreamp.h"
#include "../Source/dsp/PotTaper.h"
#include "TestUtils.h"

#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    using Rt = MarshallPlexiPreamp::Routing;
    using Cx = std::complex<double>;
    namespace T = TriodeSection;

    struct Knobs
    {
        Rt routing = Rt::ChannelI;
        bool low = false;
        float volI = 0.5f, volII = 0.5f, treble = 0.5f, middle = 0.5f, bass = 0.5f;
        double v2aBypassFarads = 0.0;
    };

    void apply(MarshallPlexiPreamp& amp, const Knobs& k)
    {
        amp.setRouting(k.routing);
        amp.setLowInput(k.low);
        amp.setVolumeI(k.volI); amp.setVolumeII(k.volII);
        amp.setTreble(k.treble); amp.setMiddle(k.middle); amp.setBass(k.bass);
        amp.setV2aBypassFarads(k.v2aBypassFarads);
    }

    std::vector<float> sine(double freq, double amplitude, double sampleRate, int n)
    {
        std::vector<float> v(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            v[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * i / sampleRate));
        return v;
    }

    struct Run
    {
        std::vector<float> in, out; // the settled second half of each
        MarshallPlexiPreamp::TestPoints points;
    };

    Run run(const Knobs& k, double freq, double amplitude, double sampleRate = 48000.0, int n = 32768)
    {
        MarshallPlexiPreamp amp(sampleRate);
        apply(amp, k);
        amp.setTestPointsEnabled(true);

        auto input = sine(freq, amplitude, sampleRate, n);
        std::vector<float> output(static_cast<size_t>(n));

        auto half = n / 2;
        amp.processBlock(input.data(), output.data(), half);   // settle
        amp.resetTestPoints();
        amp.processBlock(input.data() + half, output.data() + half, n - half);

        Run r;
        r.in.assign(input.begin() + half, input.end());
        r.out.assign(output.begin() + half, output.end());
        r.points = amp.getTestPoints();
        return r;
    }

    double mag(const std::vector<float>& v, double f, double sr = 48000.0) { return TestUtils::goertzelMagnitude(v, f, sr); }

    // Gain in dB at one frequency: output vs the same measurement of the input.
    double gainDb(const Run& r, double f, double sr = 48000.0) { return TestUtils::toDb(mag(r.out, f, sr) / mag(r.in, f, sr)); }

    // Small enough that every tube is in its linear region.
    constexpr double tiny = 0.0005;

    double smallSignalDb(const Knobs& k, double f) { return gainDb(run(k, f, tiny), f); }

    // Hann-windowed copy: raw rectangular Goertzel leaks the fundamental into
    // every harmonic bin (~-45dB each) and would read as distortion.
    std::vector<float> hann(const std::vector<float>& v)
    {
        std::vector<float> w(v.size());
        for (size_t i = 0; i < v.size(); ++i)
            w[i] = v[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(v.size() - 1)));
        return w;
    }

    struct Harmonics { double fundamental, h2, h3, thd; };

    Harmonics harmonics(const Run& r, double f)
    {
        auto w = hann(r.out);
        Harmonics h {};
        h.fundamental = mag(w, f);
        h.h2 = mag(w, 2.0 * f) / h.fundamental;
        h.h3 = mag(w, 3.0 * f) / h.fundamental;
        double sum = 0.0;
        for (int n = 2; n <= 8; ++n)
        {
            auto m = mag(w, f * n);
            sum += m * m;
        }
        h.thd = std::sqrt(sum) / h.fundamental;
        return h;
    }

    double peakOf(const std::vector<float>& v)
    {
        double p = 0.0;
        for (auto y : v) p = std::max(p, static_cast<double>(std::abs(y)));
        return p;
    }

    // ---- The hand netlist: the drawn mixer, typed out afresh and solved in the
    // frequency domain with complex Gaussian elimination (the code under test
    // uses trapezoidal companion models and a cached matrix inverse).

    std::vector<Cx> solve(std::vector<std::vector<Cx>> a, std::vector<Cx> b)
    {
        auto n = static_cast<int>(b.size());
        auto at = [&](int r, int c) -> Cx& { return a[static_cast<size_t>(r)][static_cast<size_t>(c)]; };
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs(at(r, col)) > std::abs(at(pivot, col)))
                    pivot = r;
            std::swap(a[static_cast<size_t>(col)], a[static_cast<size_t>(pivot)]);
            std::swap(b[static_cast<size_t>(col)], b[static_cast<size_t>(pivot)]);
            for (int r = col + 1; r < n; ++r)
            {
                auto f = at(r, col) / at(col, col);
                for (int c = col; c < n; ++c)
                    at(r, c) -= f * at(col, c);
                b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(col)];
            }
        }
        std::vector<Cx> x(static_cast<size_t>(n));
        for (int r = n - 1; r >= 0; --r)
        {
            auto s = b[static_cast<size_t>(r)];
            for (int c = r + 1; c < n; ++c)
                s -= at(r, c) * x[static_cast<size_t>(c)];
            x[static_cast<size_t>(r)] = s / at(r, r);
        }
        return x;
    }

    // V(V2-A grid) / V(driven plate's open-circuit voltage).
    double mixerGain(double volI, double volII, bool drivesI, double f)
    {
        enum { pI, aI, wI, pII, aII, wII, g, count };
        std::vector<std::vector<Cx>> y(count, std::vector<Cx>(count, 0.0));
        std::vector<Cx> b(count, 0.0);
        Cx s(0.0, 2.0 * M_PI * f);

        auto add = [&](int p, int q, Cx admittance) {
            y[static_cast<size_t>(p)][static_cast<size_t>(p)] += admittance;
            if (q >= 0)
            {
                y[static_cast<size_t>(q)][static_cast<size_t>(q)] += admittance;
                y[static_cast<size_t>(p)][static_cast<size_t>(q)] -= admittance;
                y[static_cast<size_t>(q)][static_cast<size_t>(p)] -= admittance;
            }
        };
        auto res = [](double ohms) { return 1.0 / std::max(ohms, 1.0); };

        // Each plate: 100k in parallel with the tube's 62.5k, behind a 1V source
        // on the driven one and to ground on the other.
        auto rs = 1.0 / (1.0 / 100.0e3 + 1.0 / 62.5e3);
        add(pI, -1, res(rs)); add(pII, -1, res(rs));
        b[static_cast<size_t>(drivesI ? pI : pII)] = res(rs);

        auto fI = potFraction(volI, 0.15), fII = potFraction(volII, 0.15);

        add(pI, aI, s * 0.0022e-6);                  // High Treble coupling
        add(aI, wI, res(1.0e6 * (1.0 - fI)));        // Volume I, upper
        add(wI, -1, res(1.0e6 * fI));                //           lower
        add(aI, wI, s * 0.005e-6);                   // bright cap
        add(wI, g, res(470.0e3));
        add(wI, g, s * 500.0e-12);

        add(pII, aII, s * 0.022e-6);                 // Normal coupling
        add(aII, wII, res(1.0e6 * (1.0 - fII)));
        add(wII, -1, res(1.0e6 * fII));
        add(wII, g, res(470.0e3));

        add(g, -1, s * 100.0e-12);                   // V2-A's grid

        return std::abs(solve(y, b)[g]);
    }

    // The cathode-shelf response of a bypassed cathode resistor.
    double shelfGain(double cathodeOhms, double bypassFarads, double f)
    {
        auto n = 1.0 + T::gm * cathodeOhms;
        Cx src(0.0, 2.0 * M_PI * f * cathodeOhms * bypassFarads);
        return std::abs((1.0 + src) / (n + src));
    }

    constexpr double stageGain = T::mu * 100.0e3 / (T::rp + 100.0e3);
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // --- 1. Operating points. ---
    std::printf("=== DC operating points ===\n");
    {
        MarshallPlexiPreamp amp(48000.0);
        auto& o = amp.getOperatingPoints();
        auto pr = [](const char* n, const T::DcPoint& d) {
            std::printf("  %-13s %.3f mA, grid %+.2f V, plate %.1f V, cathode %.2f V\n", n, d.amps * 1e3, d.gridBias, d.plateVolts, d.cathodeVolts);
        };
        pr("V1 Normal", o.v1Normal); pr("V1 High Treble", o.v1HighTreble); pr("V2-A", o.v2a); pr("V2-B follower", o.cathodeFollower);
        std::printf("  follower knee: %.1f V of V2-A plate swing\n", o.followerKneeVolts);

        auto consistent = [](const T::DcPoint& d, double supply, double rLoad, double rCathode) {
            auto curve = T::plateCurrent(d.gridBias, d.plateToCathode);
            return std::abs(curve - d.amps) < 1.0e-9
                && std::abs(d.cathodeVolts - rCathode * d.amps) < 1.0e-6
                && std::abs(d.plateVolts - (supply - rLoad * d.amps)) < 1.0e-6
                && std::abs(d.gridBias + d.cathodeVolts) < 1.0e-6;
        };
        check(consistent(o.v1Normal, 270.0, 100.0e3, 820.0), "V1 Normal (820 ohm): Ia obeys the tube curve, Ohm's law and the 100k load");
        check(consistent(o.v1HighTreble, 270.0, 100.0e3, 2.7e3), "V1 High Treble (2.7k): the same");
        check(consistent(o.v2a, 300.0, 100.0e3, 820.0), "V2-A (820 ohm): the same");
        check(std::abs(T::plateCurrent(o.cathodeFollower.gridBias, o.cathodeFollower.plateToCathode) - o.cathodeFollower.amps) < 1.0e-9
                  && std::abs(o.cathodeFollower.cathodeVolts - 100.0e3 * o.cathodeFollower.amps) < 1.0e-6,
              "V2-B follower (100k): Ia obeys the tube curve and Ohm's law");
        check(std::abs(o.cathodeFollower.cathodeVolts - o.v2a.plateVolts) < 3.0,
              "the follower's cathode sits within a few volts of the V2-A plate that drives it (gain just under 1)");
        check(o.v1Normal.amps > o.v1HighTreble.amps && o.v1HighTreble.gridBias < o.v1Normal.gridBias,
              "the 2.7k half runs colder and more negatively biased than the 820 ohm half");

        // The c.1967 drawing labels +270V at V1's plate loads and +300V at V2's,
        // dropped through 10k each from +375V: about 3.0mA into V1's node and
        // 7.5mA into V2's, which also feeds V1. The labels are hand-drawn,
        // rounded figures, so only the order of magnitude is asked for.
        auto v1Ma = (o.v1Normal.amps + o.v1HighTreble.amps) * 1e3;
        auto v2Ma = v1Ma + (o.v2a.amps + o.cathodeFollower.amps) * 1e3;
        std::printf("  supply current into V1's node %.2f mA (labelled drop implies 3.0), into V2's %.2f mA (7.5)\n", v1Ma, v2Ma);
        check(v1Ma > 0.3 * 3.0 && v1Ma < 1.5 * 3.0 && v2Ma > 0.3 * 7.5 && v2Ma < 1.5 * 7.5,
              "currents are the right order against the drawn 10k droppers (within 0.3x - 1.5x)");

        check(o.followerKneeVolts >= 8.0 && o.followerKneeVolts <= 60.0, "the follower's limiting knee is a plausible few-to-tens of volts");
    }
    std::printf("\n");

    // --- 2. The mixer against a hand netlist. ---
    std::printf("=== Jack to V2-A's grid vs a hand netlist of the drawn mixer (0.5mV in) ===\n");
    {
        check(std::abs(potFraction(0.0, 0.15)) < 1e-12 && std::abs(potFraction(1.0, 0.15) - 1.0) < 1e-12
                  && std::abs(potFraction(0.5, 0.15) - 0.15) < 1e-9,
              "the pot-taper helper reaches 0, 15% at 12 o'clock, and 1");

        double worstI = 0.0, worstII = 0.0;
        for (auto [vi, vii] : { std::pair<double, double> { 1.0, 0.0 }, { 0.6, 0.3 }, { 0.3, 0.8 }, { 0.05, 0.05 } })
        {
            for (double f : { 60.0, 100.0, 200.0, 400.0, 1000.0, 2000.0, 4000.0 })
            {
                Knobs k;
                k.volI = static_cast<float>(vi); k.volII = static_cast<float>(vii);

                k.routing = Rt::ChannelI;
                auto rI = run(k, f, tiny);
                auto predI = tiny * stageGain * shelfGain(2.7e3, 0.68e-6, f) * mixerGain(vi, vii, true, f);
                worstI = std::max(worstI, std::abs(TestUtils::toDb(rI.points.v2aGrid / predI)));

                k.routing = Rt::ChannelII;
                auto rII = run(k, f, tiny);
                auto predII = tiny * stageGain * mixerGain(vi, vii, false, f);
                worstII = std::max(worstII, std::abs(TestUtils::toDb(rII.points.v2aGrid / predII)));
            }
        }
        std::printf("  worst disagreement over 4 volume settings x 7 frequencies: Channel I %.3f dB, Channel II %.3f dB\n", worstI, worstII);
        check(worstI < 0.3, "Channel I: coupling, bright cap, 470k/500pF, Miller C and the 2.7k/.68uF shelf match the hand netlist (0.3dB)");
        check(worstII < 0.3, "Channel II: coupling, 470k and Miller C match the hand netlist (0.3dB)");

        // What the drawing's cap values are for: both are read off the same
        // network, so state the consequences in terms of it.
        auto tilt = [&](double vol, double lo, double hi) {
            return TestUtils::toDb(mixerGain(vol, 0.0, true, hi) / mixerGain(vol, 0.0, true, lo));
        };
        auto tiltDown = tilt(0.3, 400.0, 4000.0), tiltUp = tilt(1.0, 400.0, 4000.0);
        std::printf("  Channel I, 4kHz vs 400Hz at the grid: %+.1f dB at Volume 0.3, %+.1f dB at full\n", tiltDown, tiltUp);
        check(tiltDown > tiltUp + 3.0, "the bright cap keeps Channel I's treble as its Volume comes down");
    }
    std::printf("\n");

    // --- 3. V2-A's extra cathode bypass. ---
    std::printf("=== V2-A's .68uF cathode bypass (the manual's \"Tonal Note 1\") ===\n");
    {
        // v2aPlate is V2-A's output; the same signal with the bypass made huge
        // (a solid short) shows what the .68uF gives up. Prediction: the
        // textbook shelf (1 + sRC)/(N + sRC), N = 1 + gm*Rk.
        double worst = 0.0, at60 = 0.0, at4k = 0.0;
        for (double f : { 60.0, 120.0, 250.0, 600.0, 1500.0, 4000.0 })
        {
            Knobs k; k.routing = Rt::ChannelII; k.volII = 1.0f;
            auto drawn = run(k, f, tiny).points.v2aPlate;
            k.v2aBypassFarads = 1.0;
            auto shorted = run(k, f, tiny).points.v2aPlate;
            auto measured = TestUtils::toDb(drawn / shorted);
            auto predicted = TestUtils::toDb(shelfGain(820.0, 0.68e-6, f));
            worst = std::max(worst, std::abs(measured - predicted));
            if (f == 60.0) at60 = measured;
            if (f == 4000.0) at4k = measured;
        }
        std::printf("  vs a fully bypassed V2-A: %+.1f dB at 60Hz, %+.1f dB at 4kHz (worst disagreement with the shelf formula %.2f dB)\n", at60, at4k, worst);
        check(worst < 0.4, "the .68uF bypass gives exactly the textbook shelf");
        check(at60 < -5.0 && std::abs(at4k) < 0.3, "it costs the bass ~7dB and leaves the top alone - i.e. it lifts the upper mids and treble against the bass");
    }
    std::printf("\n");

    // --- 4. What the manual says about jacks and channels. ---
    std::printf("=== The manual's claims: the Low jack, and the two channels ===\n");
    {
        for (auto rt : { Rt::ChannelI, Rt::ChannelII })
        {
            Knobs k; k.routing = rt; k.volI = 0.8f; k.volII = 0.8f;
            auto hi = smallSignalDb(k, 1000.0);
            k.low = true;
            auto lo = smallSignalDb(k, 1000.0);
            std::printf("  %s: Low input %+.2f dB vs High\n", rt == Rt::ChannelI ? "Channel I " : "Channel II", lo - hi);
            check(std::abs((lo - hi) + 6.02) < 0.15, "the Low input is 6dB down");
        }

        // "Channel I: High Treble" against "Channel II: Normal": 4kHz vs 100Hz
        // through the whole preamp, tone controls at 12 o'clock.
        Knobs k; k.volI = 0.8f; k.volII = 0.8f;
        k.routing = Rt::ChannelI;
        auto tiltI = smallSignalDb(k, 4000.0) - smallSignalDb(k, 100.0);
        k.routing = Rt::ChannelII;
        auto tiltII = smallSignalDb(k, 4000.0) - smallSignalDb(k, 100.0);
        std::printf("  4kHz vs 100Hz at the output: Channel I %+.1f dB, Channel II %+.1f dB\n", tiltI, tiltII);
        check(tiltI > tiltII + 20.0, "High Treble is far brighter than Normal (>20dB of 4kHz-vs-100Hz tilt)");
        check(tiltI > 0.0 && tiltII < 0.0, "High Treble tilts bright, Normal tilts full");
    }
    std::printf("\n");

    // --- 5. The tone controls, through the whole preamp. ---
    std::printf("=== Tone controls, wired through the amp ===\n");
    {
        auto at = [&](float treble, float middle, float bass, double f) {
            Knobs k; k.routing = Rt::ChannelII; k.volII = 0.8f;
            k.treble = treble; k.middle = middle; k.bass = bass;
            return smallSignalDb(k, f);
        };
        auto bass100 = at(0.5f, 0.5f, 1.0f, 100.0) - at(0.5f, 0.5f, 0.0f, 100.0);
        auto bass4k = at(0.5f, 0.5f, 1.0f, 4000.0) - at(0.5f, 0.5f, 0.0f, 4000.0);
        auto treble4k = at(1.0f, 0.5f, 0.5f, 4000.0) - at(0.0f, 0.5f, 0.5f, 4000.0);
        auto treble100 = at(1.0f, 0.5f, 0.5f, 100.0) - at(0.0f, 0.5f, 0.5f, 100.0);
        double mid = 0.0;
        for (double f : { 400.0, 600.0, 800.0 })
            mid = std::max(mid, at(0.5f, 1.0f, 0.5f, f) - at(0.5f, 0.0f, 0.5f, f));
        std::printf("  Bass 0->max: %+.1f dB at 100Hz, %+.1f dB at 4kHz;  Treble 0->max: %+.1f dB at 4kHz, %+.1f dB at 100Hz;  Middle 0->max: up to %+.1f dB\n",
                     bass100, bass4k, treble4k, treble100, mid);
        check(bass100 > 6.0 && std::abs(bass4k) < 1.5, "the Bass knob moves the bass");
        check(treble4k > 8.0 && std::abs(treble100) < 2.0, "the Treble knob moves the treble");
        check(mid > 5.0, "the Middle knob moves the mids");
    }
    std::printf("\n");

    // --- 6. The cathode follower. ---
    std::printf("=== V2-B, the cathode follower ===\n");
    {
        MarshallPlexiPreamp amp(48000.0);
        auto knee = amp.getOperatingPoints().followerKneeVolts;
        auto a = amp.followerTransfer(0.001) / 0.001;
        auto b = amp.followerTransfer(-0.001) / -0.001;
        std::printf("  small-signal gain %.4f (+) / %.4f (-); knee %.1f V\n", a, b, knee);
        check(a > 0.97 && a < 1.0 && std::abs(a - b) < 1.0e-6, "a gain just under 1, the same both ways for small signals");
        check(std::abs(amp.followerTransfer(-100.0) / -100.0 - b) < 1.0e-9, "the negative swing is not limited");
        auto positive = amp.followerTransfer(5.0 * knee), negative = -amp.followerTransfer(-5.0 * knee);
        std::printf("  at 5x the knee: +%.1f V out for +%.1f in, -%.1f V out for -%.1f in\n", positive, 5.0 * knee, negative, 5.0 * knee);
        check(positive < 0.5 * negative, "the positive swing is limited where the negative one is not (one-sided grid conduction)");
        auto prev = 0.0;
        bool mono = true;
        for (double x = 0.0; x < 200.0; x += 1.0) { auto y = amp.followerTransfer(x); mono &= y >= prev; prev = y; }
        check(mono, "the limiting is monotonic (no fold-over)");
    }
    std::printf("\n");

    // --- 7. Behaviour when driven. ---
    std::printf("=== Distortion: level, Volume, and its character (200Hz) ===\n");
    {
        double prevI = -1.0, prevII = -1.0;
        bool riseI = true, riseII = true;
        for (double x : { 0.01, 0.03, 0.1, 0.3 })
        {
            Knobs k; k.volI = 0.5f; k.volII = 0.5f;
            k.routing = Rt::ChannelI;
            auto hI = harmonics(run(k, 200.0, x), 200.0);
            k.routing = Rt::ChannelII;
            auto hII = harmonics(run(k, 200.0, x), 200.0);
            std::printf("  %.2fV in: Channel I THD %.1f%% (H2 %.1f%%, H3 %.1f%%)   Channel II THD %.1f%% (H2 %.1f%%, H3 %.1f%%)\n",
                         x, hI.thd * 100, hI.h2 * 100, hI.h3 * 100, hII.thd * 100, hII.h2 * 100, hII.h3 * 100);
            riseI &= hI.thd > prevI; riseII &= hII.thd > prevII;
            prevI = hI.thd; prevII = hII.thd;
        }
        check(riseI && riseII, "distortion rises with playing level on both channels");

        Knobs k; k.routing = Rt::ChannelII;
        k.volII = 0.5f;
        auto clean = harmonics(run(k, 200.0, 0.001), 200.0);
        auto driven = harmonics(run(k, 200.0, 0.1), 200.0);
        check(clean.thd < 0.005, "clean at a guitar's quietest (THD < 0.5% at 1mV in)");
        check(driven.h2 > 2.0 * driven.h3, "moderate drive is mostly even-order: the follower limits one half of the swing");

        // Volume: the gain-staging. (Volume 0 is silence, so THD there is noise.)
        double prev = 0.0;
        bool volRises = true;
        for (double v : { 0.4, 0.5, 0.6, 0.8, 1.0 })
        {
            k.volII = static_cast<float>(v);
            auto h = harmonics(run(k, 200.0, 0.1), 200.0);
            std::printf("  Channel II Volume %.1f at 0.1V in: THD %.1f%%\n", v, h.thd * 100);
            volRises &= h.thd > prev;
            prev = h.thd;
        }
        check(volRises, "turning Volume II up drives V2-A harder: distortion rises (0.4 -> 1.0)");

        // Compression: a bigger guitar does not make a proportionally bigger output.
        k.volII = 0.5f;
        auto soft = harmonics(run(k, 200.0, 0.3), 200.0).fundamental;
        auto hard = harmonics(run(k, 200.0, 1.0), 200.0).fundamental;
        std::printf("  output fundamental at 0.3V in %.2f, at 1.0V in %.2f: %.2fx for 3.33x more input\n", soft, hard, hard / soft);
        check(hard / soft < 0.75 * (1.0 / 0.3), "the gain compresses when driven (well under linear)");
    }
    std::printf("\n");

    // --- 8. Volumes and the mixer. ---
    std::printf("=== Volumes and mixing ===\n");
    {
        for (auto rt : { Rt::ChannelI, Rt::ChannelII })
        {
            double prev = -1.0;
            bool mono = true;
            double at0 = 0.0, atMax = 0.0;
            for (int i = 0; i <= 10; ++i)
            {
                // The other channel's Volume stays at 0: with both up, each
                // one's wiper impedance loads the other's leg of the mixer (below).
                Knobs k; k.routing = rt;
                (rt == Rt::ChannelI ? k.volI : k.volII) = static_cast<float>(i) / 10.0f;
                (rt == Rt::ChannelI ? k.volII : k.volI) = 0.0f;
                auto r = run(k, 1000.0, tiny);
                auto level = mag(r.out, 1000.0);
                mono &= level >= prev;
                prev = level;
                if (i == 0) at0 = level;
                if (i == 10) atMax = level;
            }
            std::printf("  %s Volume 0 -> 10: %.1f dB below full at 0\n", rt == Rt::ChannelI ? "Channel I " : "Channel II", TestUtils::toDb(atMax / std::max(at0, 1e-12)));
            check(mono, "Volume is monotonic");
            check(atMax / std::max(at0, 1.0e-12) > 300.0, "Volume 0 is (nearly) silence (>50dB down)");
        }

        // The channels load each other: each 470k ends at the other channel's
        // Volume wiper, whose impedance to ground depends on where that knob
        // is. So Channel I's level at Volume 0.9 depends on Volume II - a
        // passive mixer's interaction, not two independent gains.
        {
            Knobs k; k.routing = Rt::ChannelI; k.volI = 0.9f; k.volII = 0.0f;
            auto alone = mag(run(k, 1000.0, tiny).out, 1000.0);
            double biggest = 0.0;
            for (float v2 : { 0.3f, 0.6f, 0.9f })
            {
                k.volII = v2;
                biggest = std::max(biggest, std::abs(TestUtils::toDb(mag(run(k, 1000.0, tiny).out, 1000.0) / alone)));
            }
            std::printf("  Channel I at Volume 0.9: up to %.2f dB different as Volume II moves\n", biggest);
            check(biggest > 0.3, "Channel II's Volume changes Channel I's level (the mixer's legs load each other)");
        }

        // Linear mixing: jumpered, the output is the sum of the channels alone.
        // (200Hz: where neither channel dominates - Channel I is far louder above.)
        Knobs k; k.volI = 0.6f; k.volII = 0.4f;
        k.routing = Rt::ChannelI;   auto a = run(k, 200.0, tiny);
        k.routing = Rt::ChannelII;  auto b = run(k, 200.0, tiny);
        k.routing = Rt::Jumpered;   auto both = run(k, 200.0, tiny);
        double err = 0.0, ref = 0.0;
        for (size_t i = 0; i < both.out.size(); ++i)
        {
            auto d = static_cast<double>(both.out[i]) - static_cast<double>(a.out[i]) - static_cast<double>(b.out[i]);
            err += d * d;
            ref += static_cast<double>(both.out[i]) * static_cast<double>(both.out[i]);
        }
        auto rel = std::sqrt(err / std::max(ref, 1.0e-30));
        std::printf("  jumpered vs Channel I + Channel II: relative error %.4f\n", rel);
        check(rel < 0.01, "jumpering is the sum of the two channels (a passive mixer)");
        check(std::abs(mag(both.out, 200.0) - mag(a.out, 200.0)) > 0.05 * mag(a.out, 200.0)
                  && std::abs(mag(both.out, 200.0) - mag(b.out, 200.0)) > 0.05 * mag(b.out, 200.0),
              "and it is a different signal from either channel alone");
    }
    std::printf("\n");

    // --- 9. Robustness. ---
    std::printf("=== Robustness ===\n");
    {
        MarshallPlexiPreamp amp(48000.0);
        Knobs k; k.volI = k.volII = 1.0f; k.treble = 1.0f; k.middle = 1.0f; k.bass = 1.0f;
        apply(amp, k);
        std::vector<float> zeros(48000, 0.0f), out(48000);
        amp.processBlock(zeros.data(), out.data(), 48000);
        check(peakOf(out) < 1.0e-4, "silence in gives silence out, even wide open");

        MarshallPlexiPreamp sw(48000.0);
        unsigned seed = 99u;
        bool ok = true;
        std::vector<float> in(256), o(256);
        for (int block = 0; block < 400; ++block)
        {
            for (auto& s : in) { seed = seed * 1664525u + 1013904223u; s = 1.0f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f); }
            sw.setRouting(static_cast<Rt>(block % 3));
            sw.setLowInput(block % 2 == 0);
            sw.setVolumeI(static_cast<float>(block % 7) / 6.0f);
            sw.setVolumeII(static_cast<float>(block % 5) / 4.0f);
            sw.setTreble(static_cast<float>(block % 4) / 3.0f);
            sw.setBass(static_cast<float>(block % 3) / 2.0f);
            sw.processBlock(in.data(), o.data(), 256);
            for (auto y : o) ok &= std::isfinite(y) && std::abs(y) < 500.0f;
        }
        check(ok, "switching routing, jack and every knob on every block on 1V noise stays finite and bounded");

        // reset() returns to a clean slate: the same input gives the same output.
        MarshallPlexiPreamp r1(48000.0);
        Knobs kk; kk.routing = Rt::Jumpered;
        apply(r1, kk);
        auto input = sine(300.0, 0.2, 48000.0, 8192);
        std::vector<float> first(8192), second(8192);
        r1.processBlock(input.data(), first.data(), 8192);
        r1.reset();
        r1.processBlock(input.data(), second.data(), 8192);
        double maxDiff = 0.0;
        for (size_t i = 0; i < first.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::abs(first[i] - second[i])));
        check(maxDiff < 1.0e-5, "reset() clears every filter's memory (a replay is identical)");
    }
    std::printf("\n");

    // --- 10. The host sample rate doesn't change the sound. ---
    std::printf("=== Sample-rate independence ===\n");
    {
        for (auto rt : { Rt::ChannelI, Rt::ChannelII })
        {
            Knobs k; k.routing = rt; k.volI = k.volII = 0.4f;
            double lvl[3], tilt[3];
            int i = 0;
            for (auto sr : { 44100.0, 48000.0, 96000.0 })
            {
                auto a = run(k, 1000.0, 0.01, sr, 32768), b = run(k, 100.0, 0.01, sr, 32768), c = run(k, 4000.0, 0.01, sr, 32768);
                lvl[i] = gainDb(a, 1000.0, sr);
                tilt[i] = gainDb(c, 4000.0, sr) - gainDb(b, 100.0, sr);
                ++i;
            }
            std::printf("  %s: 1kHz gain %.2f / %.2f / %.2f dB, 4kHz-vs-100Hz tilt %.2f / %.2f / %.2f dB (44.1 / 48 / 96 kHz)\n",
                         rt == Rt::ChannelI ? "Channel I " : "Channel II", lvl[0], lvl[1], lvl[2], tilt[0], tilt[1], tilt[2]);
            check(std::abs(lvl[0] - lvl[1]) < 0.5 && std::abs(lvl[2] - lvl[1]) < 0.5, "1kHz gain agrees across sample rates (0.5dB)");
            check(std::abs(tilt[0] - tilt[1]) < 1.0 && std::abs(tilt[2] - tilt[1]) < 1.0, "spectral tilt agrees across sample rates (1dB)");
        }
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
