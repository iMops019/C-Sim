#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// Why a guitar amp's Presence and Resonance controls exist, modeled: the
// amp is a feedback loop driving a LOAD that is not a resistor.
//
// THE SPEAKER'S IMPEDANCE is a curve, not a number. Its voice coil is a
// resistance in series with an inductance (impedance climbing at high
// frequencies), and the cone's mechanical resonance shows up electrically
// as a big impedance PEAK around 75-100 Hz (classic Celestions resonate
// near 75 Hz, rising to 90-100 Hz in a large closed-back cabinet - Celestion
// itself). Here that's the standard Thiele-Small equivalent circuit:
//     Z_speaker(s) = Re + s*Le + [ Res || Cmes || Lces ]
//     Res = Re*Qms/Qes,  Cmes = Qes/(2*pi*fs*Re),  Lces = Re/(2*pi*fs*Qes)
//
// THE AMP is a voltage amplifier of open-loop gain A behind an output
// resistance R0, with a fraction beta of its output voltage fed back to
// the input (negative feedback), so the closed loop is
//     H(s) = A*d(s) / (1 + A*d(s)*beta(s)),   d(s) = Z_spk / (Z_spk + R0)
// where d is the voltage divider between the amp's output resistance and
// the speaker. Two limits tell the whole story:
//   * a LOT of feedback (A*d*beta >> 1): H -> 1/beta. The load drops out.
//     The amp is a stiff VOLTAGE SOURCE (high damping factor) and the
//     speaker's impedance curve does almost nothing to the sound.
//   * LITTLE feedback: H -> A*d. The output follows the load's divider:
//     a CURRENT-SOURCE-like amp that drives more voltage into the
//     speaker's high-impedance regions - the resonance peak and the
//     inductive treble rise - accentuating them.
//
// PRESENCE and RESONANCE are the two knobs that make beta frequency-
// dependent, which is exactly how the patent literature describes them: a
// presence circuit "reduces feedback with increasing frequency above a
// selected level", a resonance circuit "reduces voltage feedback with
// decreasing frequencies below a selected level". So with beta(s) =
// beta0 * P(s) * R(s):
//     P(s) = (1 - pk) + pk * wp/(s + wp)   (1 at lows, 1-pk at highs)
//     R(s) = 1 - rk * wr/(s + wr)          (1-rk at lows, 1 at highs)
// turning Presence up removes feedback at high frequencies, raising both
// the gain there and the amp's output impedance there (so the treble
// rise of the speaker's inductance now shows through); Resonance does
// the same below ~250 Hz, where the speaker's impedance peak lives (bass
// "blooms"). Turned down, the feedback is restored and the region tightens.
//
// Implemented as a 5-state circuit (voice-coil current, the mechanical
// resonance's voltage and inductor current, and the two feedback-shaping
// filters) converted to discrete time with the bilinear (Tustin)
// transform in balanced state-space form - so the digital filter's
// response is EXACTLY the analog one at the pre-warped frequency, and the
// states are physical (currents and voltages), which means changing
// Presence/Resonance while playing carries the state across cleanly.
//
// THE NEUTRAL POINT. The raw closed loop is never flat - even the knobs'
// middle position removes feedback, and the speaker's impedance curve
// shows through - so the Filter is the closed loop DIVIDED BY ITS OWN
// NEUTRAL RESPONSE (both knobs at 0.5). Position 0.5 is therefore exactly
// flat, and each knob deviates from there with the shape the physics
// gives it: turned up the feedback is removed and the load's curve comes
// through; turned down the feedback is restored and it tightens. (This
// matches the "50 = flat" convention the OR60 add-on already had.) The
// neutral response is minimum-phase (its zeros are the speaker's
// impedance zeros - passive, so left-half-plane - and two real feedback-
// filter poles), so its inverse is stable and the division is exact.
//
// Judgment calls (not sourced): the speaker's Re/Le/Qms/Qes (a typical 8
// ohm 12"; only fs is sourced), the amp constants A/beta0/R0 (chosen for
// ~18 dB of midband feedback and R0 = 2x the nominal load, typical of a
// tube power stage), the two corner frequencies (Presence 1 kHz, Resonance
// 250 Hz), and how far each knob removes feedback (a knob at full shunts
// all of it in its band, as a real presence pot can). Framework-agnostic (no JUCE) so it can be tested
// against the physics.
namespace AmpSpeakerLoad
{
    struct Speaker
    {
        double re = 6.5;      // voice-coil DC resistance (ohms) - a typical 8 ohm nominal 12"
        double le = 0.0007;   // voice-coil inductance (H)
        double fs = 85.0;     // mechanical resonance (Hz) - closed-back guitar cab, per Celestion 75-100 Hz
        double qms = 3.5;     // mechanical Q -> an impedance peak of ~40 ohms
        double qes = 0.7;     // electrical Q
        bool resistive = false; // test/reference only: ignore all of the above, a flat 8 ohm load
    };

    struct Amp
    {
        double openLoopGain = 40.0; // A
        double beta0 = 0.65;        // feedback fraction: A*beta0 = 26, ~18 dB of feedback
        double outputOhms = 16.0;   // R0: the power stage's open-loop output resistance (2x nominal load)
        double presenceCornerHz = 1000.0;
        double resonanceCornerHz = 250.0;
        double maxFeedbackRemoved = 1.0;  // a knob at full shunts ALL of the feedback in its band (as a real presence pot can)
    };

    constexpr double pi = 3.14159265358979323846;

    inline bool sameSpeaker(const Speaker& a, const Speaker& b) noexcept
    {
        return a.re == b.re && a.le == b.le && a.fs == b.fs && a.qms == b.qms && a.qes == b.qes && a.resistive == b.resistive;
    }

    // The electrical model of each Cabinet cab type's speaker(s), in the same
    // order as CabImpulseResponse::CabType (0 V30 4x12, 1 Greenback 4x12,
    // 2 open-back 2x12, 3 1x12 combo, 4 Fender 1x10, 5 Fender 2x10, 6 Fender
    // 1x12, 7 Fender 4x12). What differs is the resonance frequency - the
    // one thing with a source: Celestion says classic speakers resonate
    // around 75 Hz, rising to 90-100 Hz in a large closed-back cabinet, and a
    // smaller cone resonates higher - and, for the 10" cones, a lower
    // voice-coil resistance and inductance. Every number is a judgment call
    // in that spirit; the point is that the amp's Resonance boost lands on
    // the speaker actually selected.
    inline Speaker speakerForCab(int cabType) noexcept
    {
        Speaker s;
        switch (cabType)
        {
            case 0:  s.fs = 90.0;  break;                          // 4x12 V30, closed back
            case 1:  s.fs = 82.0;  break;                          // 4x12 Greenback, closed back
            case 2:  s.fs = 75.0;  break;                          // open-back 2x12: no enclosure to raise it
            case 3:  s.fs = 95.0;  break;                          // 1x12 combo: a small, boxy enclosure
            case 4:  s.fs = 115.0; s.re = 6.0; s.le = 0.0005; break; // Fender 1x10: small cone
            case 5:  s.fs = 105.0; s.re = 6.0; s.le = 0.0005; break; // Fender 2x10
            case 6:  s.fs = 85.0;  break;                          // Fender 1x12
            case 7:  s.fs = 85.0;  break;                          // Fender 4x12
            default: break;
        }
        return s;
    }

    // Two speakers heard together (Spk Mix): parameters blended, with the
    // resonance frequency interpolated on a log scale (frequencies blend by
    // ratio, not by difference).
    inline Speaker mixSpeakers(const Speaker& a, const Speaker& b, double mix01) noexcept
    {
        auto t = std::min(1.0, std::max(0.0, mix01));
        if (t <= 0.0) return a;
        if (t >= 1.0) return b;

        Speaker s = a;
        s.re = a.re + (b.re - a.re) * t;
        s.le = a.le + (b.le - a.le) * t;
        s.qms = a.qms + (b.qms - a.qms) * t;
        s.qes = a.qes + (b.qes - a.qes) * t;
        s.fs = a.fs * std::pow(b.fs / a.fs, t);
        return s;
    }

    // The speaker's impedance at one frequency.
    inline std::complex<double> speakerImpedance(const Speaker& sp, double freqHz)
    {
        if (sp.resistive)
            return { 8.0, 0.0 };

        auto w = 2.0 * pi * freqHz;
        auto s = std::complex<double>(0.0, w);
        auto res = sp.re * sp.qms / sp.qes;
        auto cmes = sp.qes / (2.0 * pi * sp.fs * sp.re);
        auto lces = sp.re / (2.0 * pi * sp.fs * sp.qes);
        auto mechanical = 1.0 / (1.0 / res + s * cmes + 1.0 / (s * lces));
        return sp.re + s * sp.le + mechanical;
    }

    // The closed-loop response at an ANALOG frequency omega (rad/s), un-normalised.
    inline std::complex<double> analogResponse(const Speaker& sp, const Amp& amp, double presence01, double resonance01, double omega)
    {
        auto s = std::complex<double>(0.0, omega);
        auto pk = amp.maxFeedbackRemoved * std::min(1.0, std::max(0.0, presence01));
        auto rk = amp.maxFeedbackRemoved * std::min(1.0, std::max(0.0, resonance01));
        auto wp = 2.0 * pi * amp.presenceCornerHz;
        auto wr = 2.0 * pi * amp.resonanceCornerHz;

        auto z = speakerImpedance(sp, omega / (2.0 * pi));
        auto d = z / (z + amp.outputOhms);
        auto presenceWeight = (1.0 - pk) + pk * wp / (s + wp);
        auto resonanceWeight = 1.0 - rk * wr / (s + wr);
        auto beta = amp.beta0 * presenceWeight * resonanceWeight;
        return amp.openLoopGain * d / (1.0 + amp.openLoopGain * d * beta);
    }

    // The closed loop's own SHAPE (the physics view): gain in dB at a real
    // frequency relative to the neutral setting's 500 Hz gain. Not flat
    // even at neutral - this is what the amp + speaker actually do.
    inline double shapeDb(const Speaker& sp, const Amp& amp, double presence01, double resonance01, double freqHz)
    {
        auto reference = std::abs(analogResponse(sp, amp, 0.5, 0.5, 2.0 * pi * 500.0));
        auto h = std::abs(analogResponse(sp, amp, presence01, resonance01, 2.0 * pi * freqHz));
        return 20.0 * std::log10(h / reference);
    }

    // What the Filter below does (the analog ideal): the shape relative to
    // the NEUTRAL shape at the same frequency. Exactly 0 dB everywhere at
    // Presence = Resonance = 0.5.
    inline double deviationDb(const Speaker& sp, const Amp& amp, double presence01, double resonance01, double freqHz)
    {
        auto h = std::abs(analogResponse(sp, amp, presence01, resonance01, 2.0 * pi * freqHz));
        auto neutral = std::abs(analogResponse(sp, amp, 0.5, 0.5, 2.0 * pi * freqHz));
        return 20.0 * std::log10(h / neutral);
    }

    // Continuous-time state space and its Tustin-discretised twin.
    struct Continuous
    {
        static constexpr int order = 5;
        using Vec = std::array<double, order>;
        using Mat = std::array<Vec, order>;
        Mat a{};
        Vec b{}, c{};
        double d = 0.0;
    };

    struct Discrete
    {
        Continuous::Mat ad{};
        Continuous::Vec bd{}, cd{};
        double dd = 0.0;
        Continuous::Vec x{};

        void reset() noexcept { x.fill(0.0); }

        // y = Cd x + Dd u ; x <- Ad x + Bd u
        float step(float input) noexcept
        {
            auto u = static_cast<double>(input);
            double y = dd * u;
            for (size_t i = 0; i < static_cast<size_t>(Continuous::order); ++i)
                y += cd[i] * x[i];

            Continuous::Vec next{};
            for (size_t r = 0; r < static_cast<size_t>(Continuous::order); ++r)
            {
                double sum = bd[r] * u;
                for (size_t k = 0; k < static_cast<size_t>(Continuous::order); ++k)
                    sum += ad[r][k] * x[k];
                next[r] = sum;
            }
            x = next;
            return static_cast<float>(y);
        }
    };

    // One channel: the closed loop at the requested Presence/Resonance,
    // followed by the inverse of the closed loop at the NEUTRAL setting -
    // see the header comment. setControls() rebuilds only the forward
    // system (a 5x5 matrix inverse - cheap, and only when a knob moved);
    // the neutral inverse is built once in prepare().
    class Filter
    {
    public:
        static constexpr int order = Continuous::order;
        using Vec = Continuous::Vec;
        using Mat = Continuous::Mat;

        explicit Filter(Speaker speakerToUse = {}, Amp ampToUse = {}) : speaker(speakerToUse), amp(ampToUse) {}

        void prepare(double sampleRate)
        {
            fs = sampleRate;
            neutralInverse = tustin(inverseOf(build(0.5, 0.5)));
            haveControls = false;
            prepared = true;
            setControls(0.5f, 0.5f);
            reset();
        }

        // Swap the speaker the amp is driving (e.g. when the Cabinet's cab
        // changes). Rebuilds both halves - the closed loop and the neutral
        // inverse it is divided by - so the neutral setting stays exactly
        // flat for any speaker. The running states are physical (currents
        // and voltages) and are carried across; a switch between two very
        // different speakers can still make a brief transient.
        void setSpeaker(const Speaker& newSpeaker)
        {
            if (sameSpeaker(speaker, newSpeaker))
                return;
            speaker = newSpeaker;
            if (! prepared)
                return;

            auto forwardState = forward.x;
            auto inverseState = neutralInverse.x;
            neutralInverse = tustin(inverseOf(build(0.5, 0.5)));
            neutralInverse.x = inverseState;
            forward = tustin(build(std::min(1.0f, std::max(0.0f, lastPresence)), std::min(1.0f, std::max(0.0f, lastResonance))));
            forward.x = forwardState;
        }

        const Speaker& getSpeaker() const noexcept { return speaker; }

        void reset() noexcept
        {
            forward.reset();
            neutralInverse.reset();
        }

        void setControls(float presence01, float resonance01)
        {
            if (haveControls && presence01 == lastPresence && resonance01 == lastResonance)
                return;
            haveControls = true;
            lastPresence = presence01;
            lastResonance = resonance01;

            // Keep the running states: they are physical (currents and
            // voltages), so a knob move carries across without a click.
            auto state = forward.x;
            forward = tustin(build(std::min(1.0f, std::max(0.0f, presence01)), std::min(1.0f, std::max(0.0f, resonance01))));
            forward.x = state;
        }

        float processSample(float input) noexcept { return neutralInverse.step(forward.step(input)); }

    private:
        static double dot(const Vec& a, const Vec& b) noexcept
        {
            double sum = 0.0;
            for (size_t i = 0; i < static_cast<size_t>(order); ++i)
                sum += a[i] * b[i];
            return sum;
        }

        // Gauss-Jordan inverse with partial pivoting.
        static Mat inverse(Mat m)
        {
            Mat inv{};
            for (size_t i = 0; i < static_cast<size_t>(order); ++i)
                inv[i][i] = 1.0;

            for (size_t col = 0; col < static_cast<size_t>(order); ++col)
            {
                size_t pivot = col;
                for (size_t r = col + 1; r < static_cast<size_t>(order); ++r)
                    if (std::abs(m[r][col]) > std::abs(m[pivot][col]))
                        pivot = r;
                std::swap(m[col], m[pivot]);
                std::swap(inv[col], inv[pivot]);

                auto p = m[col][col];
                for (size_t c = 0; c < static_cast<size_t>(order); ++c)
                {
                    m[col][c] /= p;
                    inv[col][c] /= p;
                }
                for (size_t r = 0; r < static_cast<size_t>(order); ++r)
                {
                    if (r == col) continue;
                    auto f = m[r][col];
                    for (size_t c = 0; c < static_cast<size_t>(order); ++c)
                    {
                        m[r][c] -= f * m[col][c];
                        inv[r][c] -= f * inv[col][c];
                    }
                }
            }
            return inv;
        }

        // The closed loop as a continuous state space. States: [i, vm, iL,
        // sp, sr] - the loop current (through Re and Le), the mechanical
        // resonance's voltage and inductor current, and the two feedback-
        // shaping low-passes.
        Continuous build(double presence01, double resonance01) const
        {
            auto pk = amp.maxFeedbackRemoved * presence01;
            auto rk = amp.maxFeedbackRemoved * resonance01;
            auto wp = 2.0 * pi * amp.presenceCornerHz;
            auto wr = 2.0 * pi * amp.resonanceCornerHz;
            auto aBeta = amp.openLoopGain * amp.beta0;

            // The output voltage is an algebraic function of the states
            // and the input (solved from the feedback loop):
            //   Vn = c_in*Vin + c_i*i + c_sp*sp + c_sr*sr
            auto g = 1.0 + aBeta * (1.0 - pk);
            auto cIn = amp.openLoopGain / g;
            auto cI = -amp.outputOhms / g;
            auto cSp = -aBeta * pk / g;
            auto cSr = aBeta * rk / g;

            auto re = speaker.re, le = speaker.le;
            auto cm = speaker.qes / (2.0 * pi * speaker.fs * speaker.re);
            auto lc = speaker.re / (2.0 * pi * speaker.fs * speaker.qes);
            auto resistor = speaker.re * speaker.qms / speaker.qes;

            Continuous sys;
            sys.a[0] = { (cI - re) / le, -1.0 / le, 0.0, cSp / le, cSr / le };
            sys.b[0] = cIn / le;
            sys.a[1] = { 1.0 / cm, -1.0 / (resistor * cm), -1.0 / cm, 0.0, 0.0 };
            sys.a[2] = { 0.0, 1.0 / lc, 0.0, 0.0, 0.0 };
            sys.a[3] = { wp * cI, 0.0, 0.0, wp * (cSp - 1.0), wp * cSr };
            sys.b[3] = wp * cIn;
            sys.a[4] = { wr * (1.0 - pk) * cI, 0.0, 0.0, wr * ((1.0 - pk) * cSp + pk), wr * ((1.0 - pk) * cSr - 1.0) };
            sys.b[4] = wr * (1.0 - pk) * cIn;
            sys.c = { cI, 0.0, 0.0, cSp, cSr };
            sys.d = cIn;
            return sys;
        }

        // The inverse system 1/H(s) of a biproper system (D != 0):
        //   A' = A - B*C/D,  B' = B/D,  C' = -C/D,  D' = 1/D.
        static Continuous inverseOf(const Continuous& h)
        {
            Continuous inv;
            for (size_t r = 0; r < static_cast<size_t>(order); ++r)
                for (size_t c = 0; c < static_cast<size_t>(order); ++c)
                    inv.a[r][c] = h.a[r][c] - h.b[r] * h.c[c] / h.d;
            for (size_t i = 0; i < static_cast<size_t>(order); ++i)
            {
                inv.b[i] = h.b[i] / h.d;
                inv.c[i] = -h.c[i] / h.d;
            }
            inv.d = 1.0 / h.d;
            return inv;
        }

        // Bilinear (Tustin), balanced form:
        //   Ad = (I - aA)^-1 (I + aA)      Bd = sqrt(2a) (I - aA)^-1 B
        //   Cd = sqrt(2a) C (I - aA)^-1    Dd = D + a C (I - aA)^-1 B      with a = T/2
        Discrete tustin(const Continuous& sys) const
        {
            auto alpha = 0.5 / fs;
            Mat iMinus{}, iPlus{};
            for (size_t r = 0; r < static_cast<size_t>(order); ++r)
                for (size_t col = 0; col < static_cast<size_t>(order); ++col)
                {
                    auto identity = (r == col) ? 1.0 : 0.0;
                    iMinus[r][col] = identity - alpha * sys.a[r][col];
                    iPlus[r][col] = identity + alpha * sys.a[r][col];
                }
            auto m = inverse(iMinus);

            Discrete out;
            for (size_t r = 0; r < static_cast<size_t>(order); ++r)
                for (size_t col = 0; col < static_cast<size_t>(order); ++col)
                {
                    double sum = 0.0;
                    for (size_t k = 0; k < static_cast<size_t>(order); ++k)
                        sum += m[r][k] * iPlus[k][col];
                    out.ad[r][col] = sum;
                }

            auto root = std::sqrt(2.0 * alpha);
            Vec mb{};
            for (size_t r = 0; r < static_cast<size_t>(order); ++r)
            {
                double sum = 0.0;
                for (size_t k = 0; k < static_cast<size_t>(order); ++k)
                    sum += m[r][k] * sys.b[k];
                mb[r] = sum;
                out.bd[r] = root * sum;
            }
            for (size_t col = 0; col < static_cast<size_t>(order); ++col)
            {
                double sum = 0.0;
                for (size_t k = 0; k < static_cast<size_t>(order); ++k)
                    sum += sys.c[k] * m[k][col];
                out.cd[col] = root * sum;
            }
            out.dd = sys.d + alpha * dot(sys.c, mb);
            return out;
        }

        Speaker speaker;
        Amp amp;
        double fs = 48000.0;
        bool haveControls = false;
        bool prepared = false;
        float lastPresence = 0.5f, lastResonance = 0.5f;

        Discrete forward, neutralInverse;
    };
}
