#pragma once

#include "Oversampler4x.h"

// A solid-state power amp stage - the other half of the Tube/Solid-State
// choice PowerAmpPedal offers alongside PowerAmpStage (tube). The
// defining, audible contrast the user actually cares about isn't a watts
// number, it's this: a solid-state output stage runs off a stiff,
// regulated rail rather than a tube's B+ supply, so it doesn't sag under
// sustained demand, and it stays clean over a much wider input range
// before its own clipping kicks in - but once it does clip, the onset is
// harder/more abrupt than a tube's smooth, gradually-compressing curve.
// Common real-world pairing: a preamp pedal/profiler into a solid-state
// power amp (Matrix GT1000FX, Crown, etc.) driving a real cab, a
// well-known rig style in modern metal/djent.
//
// Deliberately simpler than PowerAmpStage: no sag envelope (a stiff
// rail doesn't droop the way a tube B+ supply does - modeling one here
// would misrepresent the exact thing that makes this stage sound
// different) and no explicit push-pull combination step, since the
// clipping curve below is already an odd function by construction
// (sign(x) * f(|x|)), which is what real push-pull symmetry amounts to
// - no separate +x/-x evaluate-and-combine needed the way the
// asymmetric Koren tube curve requires elsewhere in this toolkit.
class SolidStatePowerAmpStage
{
public:
    explicit SolidStatePowerAmpStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    // 0..1: how hard the input is driven into the clipping stage.
    void setDrive(float amount);

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) const noexcept;

    double sampleRate;
    float drive = 0.0f;

    Oversampler4x oversampler;

    // Stays perfectly linear up to here - the "way more headroom" a
    // stiff solid-state rail buys you over a tube stage at the same
    // drive level - then a smoothstep knee up to a hard ceiling at
    // +/-1, tighter/more abrupt than the tube stage's gradual tanh-like
    // compression.
    static constexpr float linearThreshold = 0.85f;
    static constexpr float driveRange = 5.0f; // 1x to 6x into the clipper
};
