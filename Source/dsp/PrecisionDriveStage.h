#pragma once

#include "CouplingHighpass.h"
#include "Oversampler4x.h"

// Models the Horizon Devices Precision Drive - per its own documentation
// and independent circuit analysis, its core is a Tube Screamer-style
// soft-clipping overdrive with an adjustable low cut in place of the
// classic fixed one, plus a "Bright" treble-shelf control used instead of
// a passive treble-cut tone knob (see Horizon Devices' own user guide and
// diystompboxes.com's schematic discussion). The pedal's other signature
// feature, its analog downward-expander noise gate, is reused from this
// toolkit's own dsp::NoiseGate in PrecisionDrivePedal rather than
// reimplemented here - same gating math the rest of the app already uses
// and already validated, not a second copy of it.
//
// The clipping stage itself is a smooth symmetric soft-clip (tanh), which
// is the standard, widely-used approximation for a Tube Screamer-style
// op-amp-plus-diodes *feedback-loop* clipper (its smooth, compressed
// character is what actually distinguishes a TS-style "overdrive" from a
// harder series-diode "distortion" clipper like this toolkit's
// DiodeClipperStage) - solving the real feedback-loop circuit implicitly,
// the way DiodeClipperStage solves its own (different) topology via
// Newton-Raphson, is a bigger undertaking reserved for a future pass if
// the approximation doesn't feel right. A fixed mid-frequency bump ahead
// of the clipper models the Tube Screamer family's well-documented ~720Hz
// pre-emphasis, the other half of what gives this circuit family its
// recognizable "mid-forward" character.
//
// Like every other nonlinear stage in this toolkit, the clipping step
// itself runs inside a 4x oversampled block; the linear shaping around it
// (Attack high-pass, mid bump, Bright shelf) runs at the base rate, since
// only the clip itself can generate aliasing harmonics.
class PrecisionDriveStage
{
public:
    explicit PrecisionDriveStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setAttack(float amount); // 0..1: 0 = thick/bassy (low cutoff), 1 = tight/picky (higher cutoff)
    void setDrive(float amount);  // 0..1: clipping drive
    void setBright(float amount); // 0..1: post-clip treble shelf boost

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process(float x) noexcept;
        void reset() noexcept;
    };

    static void designMidBump(Biquad& b, double sampleRate);
    static void designBrightShelf(Biquad& b, double sampleRate, float amount);

    double sampleRate;
    float driveGain = 1.5f;

    CouplingHighpass attackFilter;
    Biquad midBump;
    Biquad brightShelf;
    Oversampler4x oversampler;
};
