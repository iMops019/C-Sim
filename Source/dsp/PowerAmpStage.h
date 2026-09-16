#pragma once

#include "KorenTriodeStage.h"

// The power amp stage: push-pull output tubes, B+ supply sag, and a
// negative feedback loop - the three things step 5 of the build order
// asks for.
//
//  - Push-pull: real push-pull output stages use two tubes, each handling
//    one half of the waveform, recombined through an output transformer.
//    That cancels even-order harmonics (2nd, 4th...) that a single tube
//    would produce, leaving mostly odd ones - audibly different character
//    from a single-ended (preamp) stage. Modeled here by evaluating the
//    SAME asymmetric Koren nonlinearity at +x and -x and combining as
//    0.5*(f(x) - f(-x)) - the odd part of f, which is exactly what
//    cancelling the even part means.
//  - Sag: a real B+ supply droops under sustained current draw and
//    recovers over the supply capacitor's time constant. Modeled
//    physically (not as a bolted-on gain multiplier) by dynamically
//    lowering the Koren stage's own plate-voltage parameter based on a
//    smoothed envelope of demand - a lower Vp genuinely saturates sooner
//    and softer, which is what sag actually sounds like.
//  - Negative feedback: a fraction of the (one-sample-delayed, avoiding
//    an algebraic loop) output is subtracted from the input. Real power
//    amps trade gain for damping/tightness this way - more feedback,
//    less gain, tighter and faster-settling low end.
class PowerAmpStage
{
public:
    explicit PowerAmpStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setSag(float amount);      // 0..1: depth of B+ droop under sustained drive
    void setFeedback(float amount); // 0..1: more feedback = less gain, more damping/tightness

    void reset();

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float sagAmount = 0.4f;
    float feedbackAmount = 0.3f;

    double sagEnvelope = 0.0;
    double sagAttackCoeff = 0.0;
    double sagReleaseCoeff = 0.0;

    float previousOutput = 0.0f;

    KorenTriodeStage powerTriode;

    // Power tubes run a noticeably higher B+ rail than preamp stages.
    static constexpr double nominalPlateVoltage = 300.0;
    static constexpr double sagDepthScale = 0.5; // how much Vp can droop at full sag/full envelope
    static constexpr double minPlateVoltageFraction = 0.35; // floor, avoids a degenerate near-zero Vp
};
