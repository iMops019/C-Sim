#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// How far a mic sits from the cab changes the sound in two real, separate
// ways - both modeled here, framework-agnostic (no JUCE) so they can be
// tested against the physics in isolation:
//
//  1. ARRIVAL TIME. Sound covers ~343 m/s, so a mic 12 inches further from
//     the speaker hears it ~0.9 ms later. On its own that's inaudible; but
//     blend two mics at different distances and the offset makes their
//     signals partially cancel at some frequencies and reinforce at others
//     (comb filtering) - the reason a real close+far blend sounds "full"
//     and slightly phasey rather than like one mic turned up. Only the
//     DIFFERENCE between the mics matters, so the nearer one gets zero
//     delay and the farther one is delayed by the difference (keeps added
//     latency to ~1 ms at the very most). See FractionalDelay.
//
//  2. PROXIMITY EFFECT. A directional mic (any cardioid or figure-8 - a
//     dynamic like an SM57, a ribbon) responds partly to the pressure
//     GRADIENT across its capsule, and close to the source that gradient
//     grows: bass is boosted. The textbook response is
//         H(jw) = 1 + kappa / (j * k * r),  k = w / c
//     with kappa = 0 for an omni, 0.5 for a cardioid, 1 for a figure-8,
//     and r the distance. Written as a Laplace transfer function that's
//         H(s) = (s + kappa*c/r) / s
//     and since a mic is calibrated to be flat at some reference distance
//     r0 the RELATIVE response at r is
//         H(s) = (s + a) / (s + a0),   a = kappa*c/r,  a0 = kappa*c/r0
//     - a first-order low shelf (DC gain a/a0 = r0/r, unity at high
//     frequencies) whose zero and pole are the two "corner" frequencies.
//     At r = r0 it is exactly 1: the default sound doesn't move.
//
// Deliberately NOT modeled: level falling with distance (~ -6 dB per
// doubling). A mic placed further away is also quieter, but here every
// mic is kept at equal level so Mic Blend is the only fader and the phase
// interaction stays fully audible - an engineer would ride the fader, and
// that's what the blend knob is.
namespace MicPlacement
{
    constexpr double speedOfSound = 343.0; // m/s

    // The Distance knob (0..100) sweeps 1 inch to 18 inches, exponentially
    // (equal ratio per step - matches how distance is heard).
    constexpr double nearMetres = 0.0254;
    constexpr double farMetres = 0.4572;

    // The distance every mic is "calibrated flat" at, and what a Distance
    // knob at 50 means: the exponential midpoint of the range (~4.2 in).
    inline double referenceMetres() noexcept { return nearMetres * std::sqrt(farMetres / nearMetres); }

    inline double distanceMetres(float knob0to100) noexcept
    {
        auto x = std::min(100.0f, std::max(0.0f, knob0to100)) / 100.0;
        return nearMetres * std::pow(farMetres / nearMetres, x);
    }

    // How much later a mic at `metres` hears the source than one at
    // `otherMetres`. Zero when it's the nearer (or equal) one.
    inline double relativeDelaySeconds(double metres, double otherMetres) noexcept
    {
        return std::max(0.0, metres - otherMetres) / speedOfSound;
    }

    // kappa for the mic types the Cabinet offers. A pure cardioid is 0.5
    // and a figure-8 ribbon is 1.0; both are pulled toward omni here (an
    // undamped figure-8 close-up is a roughly 25 dB bass boost, and a
    // real cab mic's own low-end design and the speaker's own limited
    // bass keep the practical effect far smaller).
    constexpr float kappaDynamic = 0.4f;
    constexpr float kappaRibbon = 0.7f;

    // One channel of the proximity-effect shelf. y = b0*x + b1*x1 - a1*y1,
    // a bilinear transform of H(s) = (s + a) / (s + a0).
    class ProximityShelf
    {
    public:
        void prepare(double sampleRate) noexcept
        {
            fs = sampleRate;
            setPlacement(0.0f, 1.0, 1.0); // identity
            reset();
        }

        void reset() noexcept { x1 = y1 = 0.0; }

        // kappa: see kappaDynamic/kappaRibbon. distance/reference in metres.
        void setPlacement(float kappa, double distance, double reference) noexcept
        {
            auto a = static_cast<double>(kappa) * speedOfSound / distance;   // zero (rad/s)
            auto a0 = static_cast<double>(kappa) * speedOfSound / reference; // pole (rad/s)
            zeroHz = a / (2.0 * 3.14159265358979323846);
            poleHz = a0 / (2.0 * 3.14159265358979323846);

            auto k = 2.0 * fs;
            auto d = k + a0;
            b0 = (k + a) / d;
            b1 = (a - k) / d;
            a1 = (a0 - k) / d;
        }

        float processSample(float input) noexcept
        {
            auto x = static_cast<double>(input);
            auto y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return static_cast<float>(y);
        }

        double getZeroHz() const noexcept { return zeroHz; }
        double getPoleHz() const noexcept { return poleHz; }

    private:
        double fs = 48000.0;
        double b0 = 1.0, b1 = 0.0, a1 = 0.0;
        double x1 = 0.0, y1 = 0.0;
        double zeroHz = 0.0, poleHz = 0.0;
    };

    // A delay of a fractional number of samples (linear interpolation),
    // whose delay time glides toward its target with a one-pole smoother
    // instead of jumping - a jump would click, and a Distance knob turned
    // during playback shouldn't. The glide does bend pitch slightly while
    // it moves (the Doppler effect of a mic actually being moved).
    class FractionalDelay
    {
    public:
        void prepare(double sampleRate, double maxDelaySeconds, double smoothingSeconds = 0.02)
        {
            capacity = static_cast<int>(std::ceil(maxDelaySeconds * sampleRate)) + 4;
            buffer.assign(static_cast<size_t>(capacity), 0.0f);
            maxDelay = static_cast<float>(capacity - 3);
            smoothing = 1.0f - static_cast<float>(std::exp(-1.0 / (smoothingSeconds * sampleRate)));
            writePos = 0;
            current = target = 0.0f;
        }

        void reset() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            current = target;
        }

        // immediate = true skips the glide (use when there's no signal to
        // click, e.g. right after prepare()).
        void setDelaySamples(float samples, bool immediate = false) noexcept
        {
            target = std::min(maxDelay, std::max(0.0f, samples));
            if (immediate)
                current = target;
        }

        float getCurrentDelaySamples() const noexcept { return current; }

        float processSample(float input) noexcept
        {
            current += (target - current) * smoothing;

            buffer[static_cast<size_t>(writePos)] = input;

            auto whole = static_cast<int>(current);
            auto frac = current - static_cast<float>(whole);

            auto i0 = writePos - whole;
            auto i1 = i0 - 1;
            if (i0 < 0) i0 += capacity;
            if (i1 < 0) i1 += capacity;

            auto out = buffer[static_cast<size_t>(i0)] * (1.0f - frac) + buffer[static_cast<size_t>(i1)] * frac;

            if (++writePos >= capacity)
                writePos = 0;
            return out;
        }

    private:
        std::vector<float> buffer;
        int capacity = 0;
        int writePos = 0;
        float current = 0.0f, target = 0.0f, maxDelay = 0.0f, smoothing = 1.0f;
    };
}
