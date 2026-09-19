#pragma once

#include <algorithm>
#include <cmath>

// How a guitar speaker behaves when it's PUSHED - the part a captured or
// synthesized impulse response can't do, because an IR is linear: it
// sounds the same at any volume. Real speakers don't. Two effects, both
// level-dependent (quiet playing passes through untouched):
//
//  1. CONE EXCURSION LIMITING. The cone travels furthest on low
//     frequencies. Near the ends of its travel the suspension stiffens
//     (and the voice coil leaves the magnet's most uniform field), so
//     the cone can't follow the drive any further: the bass is softly
//     squashed and, because that's a nonlinearity, it makes harmonics -
//     mostly odd (3rd) from the symmetric suspension stiffening, with a
//     little even from the driver's own asymmetry. Modeled by splitting
//     off the low band (where excursion lives) and soft-saturating it,
//     leaving everything above untouched.
//
//  2. POWER COMPRESSION. Driven hard for a while, the voice coil heats,
//     its resistance rises, and the speaker turns less of the amp's
//     power into sound: the whole output sags in level, slowly. Modeled
//     as gain reduction from a slow envelope of the input, kicking in
//     above a threshold.
//
// Different speakers run out of headroom at different points. Cone
// excursion (and so the drive level it takes to reach the limit) scales
// with the SQUARE ROOT of the speaker's power rating - power goes as
// amplitude squared - so a 25 W Celestion Greenback reaches its limits
// at roughly 65% of the drive a 60 W Vintage 30 tolerates. Headroom here
// is that ratio, relative to the 60 W V30.
//
// Applied BEFORE the cab IR, where it belongs: the distortion is created
// at the cone and then shaped by the cabinet and the mic, so the harmonics
// it makes get the cab's own tonal filtering. Framework-agnostic (no JUCE)
// so it can be tested against these physical claims on its own.
//
// Judgment calls (not sourced): the excursion band's 220 Hz corner, the
// saturation/compression thresholds and depths, the asymmetry amount, and
// the compression time constants. The rated-power table below is typical
// published ratings, not a specific speaker's datasheet.
namespace SpeakerCompression
{
    constexpr float referenceWatts = 60.0f; // Celestion Vintage 30

    // Typical rated power per Cabinet cab type, in the same order as
    // CabImpulseResponse::CabType (0 V30, 1 Greenback, 2 open-back 2x12,
    // 3 1x12 combo, 4 Fender 1x10, 5 Fender 2x10, 6 Fender 1x12, 7 Fender
    // 4x12). Per speaker: a 4x12 shares the amp's power across four
    // speakers, but the pedal doesn't know the amp's wattage, so this is
    // about the speaker, not the cabinet.
    inline float ratedWattsForCab(int cabType) noexcept
    {
        static constexpr float watts[8] = { 60.0f, 25.0f, 30.0f, 30.0f, 20.0f, 25.0f, 50.0f, 50.0f };
        return watts[std::min(7, std::max(0, cabType))];
    }

    // Amplitude headroom relative to the 60 W reference: sqrt(power ratio).
    inline float headroomForWatts(float watts) noexcept { return std::sqrt(watts / referenceWatts); }

    // Judgment-call constants - see the header comment.
    constexpr double excursionCornerHz = 220.0;
    constexpr float saturationLevel = 0.4f;     // low-band level where saturation begins at Push = 100%, headroom 1
    constexpr float asymmetry = 0.25f;          // suspension/BL asymmetry (adds a little 2nd harmonic), scaled by Push
    constexpr float compressionThreshold = 0.35f; // envelope level where power compression begins, headroom 1
    constexpr float compressionDepth = 0.4f;    // gain = 1 / (1 + depth * push * (env/threshold - 1))
    constexpr double envelopeAttackSeconds = 0.010;
    constexpr double envelopeReleaseSeconds = 0.200;

    // One channel. push01 = 0 is an EXACT bypass (returns the input sample
    // untouched); from there the effect fades in continuously - the
    // saturation threshold starts at infinity and comes down as Push rises,
    // so there is no jump between "off" and "barely on".
    class Processor
    {
    public:
        void prepare(double sampleRate) noexcept
        {
            constexpr double pi = 3.14159265358979323846;

            // 2nd-order Butterworth low-pass (RBJ cookbook), the excursion band.
            auto w0 = 2.0 * pi * excursionCornerHz / sampleRate;
            auto alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
            auto cosw = std::cos(w0);
            auto a0 = 1.0 + alpha;
            lb0 = (1.0 - cosw) / 2.0 / a0;
            lb1 = (1.0 - cosw) / a0;
            lb2 = lb0;
            la1 = -2.0 * cosw / a0;
            la2 = (1.0 - alpha) / a0;

            attack = std::exp(-1.0 / (envelopeAttackSeconds * sampleRate));
            release = std::exp(-1.0 / (envelopeReleaseSeconds * sampleRate));
            reset();
        }

        void reset() noexcept
        {
            x1 = x2 = y1 = y2 = 0.0;
            envelope = 0.0;
        }

        void setPush(float push01) noexcept { push = std::min(1.0f, std::max(0.0f, push01)); }
        void setHeadroom(float relativeHeadroom) noexcept { headroom = std::max(0.05f, relativeHeadroom); }

        float getEnvelope() const noexcept { return static_cast<float>(envelope); }

        float processSample(float input) noexcept
        {
            // The filter and envelope always run, even while bypassed, so
            // their state is current the moment Push is raised (no
            // start-up transient from stale state).
            auto x = static_cast<double>(input);
            auto low = lb0 * x + lb1 * x1 + lb2 * x2 - la1 * y1 - la2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = low;

            auto rectified = std::abs(x);
            envelope = rectified > envelope ? attack * envelope + (1.0 - attack) * rectified
                                            : release * envelope + (1.0 - release) * rectified;

            if (push <= 0.0f)
                return input;

            // 1. Excursion: soft-saturate the low band. Saturation level
            // T falls as Push rises (and is lower for a speaker with less
            // headroom); the bias adds a little asymmetry. The transfer is
            // normalised so its small-signal gain is exactly 1.
            auto t = static_cast<double>(saturationLevel) * headroom / static_cast<double>(push);
            auto bias = static_cast<double>(asymmetry) * push;
            auto tanhBias = std::tanh(bias);
            auto saturatedLow = t * (std::tanh(low / t + bias) - tanhBias) / (1.0 - tanhBias * tanhBias);
            auto high = x - low;

            // 2. Power compression: slow gain reduction above a threshold.
            auto over = std::max(0.0, envelope / (static_cast<double>(compressionThreshold) * headroom) - 1.0);
            auto gain = 1.0 / (1.0 + static_cast<double>(compressionDepth) * push * over);

            return static_cast<float>(gain * (saturatedLow + high));
        }

    private:
        double lb0 = 1.0, lb1 = 0.0, lb2 = 0.0, la1 = 0.0, la2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double attack = 0.0, release = 0.0, envelope = 0.0;
        float push = 0.0f, headroom = 1.0f;
    };
}
