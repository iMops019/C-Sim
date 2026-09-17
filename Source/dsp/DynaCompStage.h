#pragma once

#include "CouplingHighpass.h"
#include "EnvelopeUtils.h"

// Models the MXR Dyna Comp (M102): a feedback-style OTA compressor built
// around a CA3080 transconductance amp, researched via ElectroSmash's
// published schematic analysis. Real, sourced circuit facts behind this
// model:
//  - Only two controls exist on the real pedal - Sensitivity and Output
//    - there is no separate Threshold/Ratio/Attack/Release, unlike
//      EarthQuaker's Warden (see WardenStage) which exposes all of
//      those independently. Sensitivity alone controls both how low the
//      effective threshold sits AND how hard gain reduction bites once
//      past it - modeled here as a single control moving both a
//      threshold and a fairly high, fairly fixed ratio together, since
//      that's the real pedal's actual behaviour, not an oversight to
//      "fix" by adding more knobs.
//  - The envelope follower is a single time constant driven by a large
//    (10uF) smoothing capacitor - ElectroSmash's own words: "will act
//    as a low pass filter, making the voltage... change slowly and
//    follow the general envelope." Modeled as one shared attack/release
//    time constant (not independently adjustable - there's no knob for
//    it on the real pedal), tuned to reproduce the fast, percussive
//    "grab" response that made this pedal a go-to for tight, palm-muted
//    rhythm playing (a widely-documented reputation, not itself sourced
//    from the schematic analysis above).
//  - This is a genuine FEEDBACK-style design: the envelope follower
//    tracks the ALREADY gain-reduced (post-compression) signal, not the
//    raw input - a real topological distinction from a feedforward
//    compressor, and part of what gives feedback-style comps their
//    characteristic self-stabilizing "settle" behaviour rather than a
//    feedforward design's more literal, immediate threshold response.
//  - Output doesn't just trim level - ElectroSmash's analysis notes it
//    "changes slightly the output filter's cutoff frequency, ranging
//    from 52Hz to 310Hz depending on the knob position," a real,
//    sourced quirk of the circuit's output coupling network, modeled
//    here as a highpass whose corner moves with the Output control
//    rather than a plain volume pot.
class DynaCompStage
{
public:
    explicit DynaCompStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setSensitivity(float amount); // 0..1 - compression amount (threshold + ratio combined, as on the real pedal)
    void setOutput(float amount);      // 0..1 - level, plus the real output-filter cutoff sweep (see header)

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float sensitivity = 0.5f;
    float outputAmount = 0.7f;
    float levelGain = 1.0f;

    float envelope = 0.0f;   // tracks the POST-gain-reduction signal - feedback-style, see header
    float lastOutput = 0.0f; // this stage's own previous output sample, what envelope actually follows
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    CouplingHighpass outputFilter;
};
