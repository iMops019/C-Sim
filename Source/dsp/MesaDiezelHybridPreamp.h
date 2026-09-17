#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// A hybrid preamp combining specific pieces of MesaRectifierPreamp and
// DiezelVH4Preamp - matching this toolkit's existing HybridAmp precedent
// of splicing shared stages together rather than crossfading two fully-
// separate final mixes (see HybridAmp's own header for that reasoning).
// The signal path:
//
//   input -> stage 1 (Diezel-style: part of a 4-stage tight cascade)
//         -> Mesa's OWN gain-dependent voicing shelf (the 656Hz-1400Hz
//            bright-cap-sweep network from MesaRectifierPreamp, applied
//            here exactly as it is there)
//         -> stages 2-4, with Diezel's own progressively tighter
//            inter-stage corners (50/90/150Hz) between them
//         -> Diezel's own Deep control (an 80Hz additive-lowpass shelf,
//            restoring the bass the tight cascade costs)
//
// The musical idea: Diezel's cascade depth and inter-stage tightness
// (four stages, corners rising to 150Hz) for a more compressed, low-end-
// limited foundation than Mesa's own 3-stage cascade has on its own,
// with Mesa's gain-dependent brightening layered on top for its
// aggressive upper-mid bite as Gain rises - and Diezel's Deep control
// to dial the low end back in either preamp couldn't offer alone (Mesa
// has no such control; Diezel's own Deep is the one being reused here).
class MesaDiezelHybridPreamp
{
public:
    explicit MesaDiezelHybridPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float newGain); // 0..1 - drives the cascade AND Mesa's voicing sweep, same coupling as in MesaRectifierPreamp
    void setDeep(float newDeep); // 0..1 - Diezel's own bass-restoring shelf

    // Mesa's voicing shelf corner, exposed for direct verification the
    // same way MesaRectifierPreamp does - see that class for why
    // measuring it acoustically through the whole cascade is confounded
    // by Gain also scaling drive.
    float getVoicingCutoffHz() const noexcept { return voicingCutoffHz; }

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 4;

    double sampleRate;
    float gain = 0.6f;
    float deep = 0.3f;
    float voicingCutoffHz = 0.0f;

    CouplingHighpass preGainHighpass;    // Diezel's own ~87Hz "tight bass" corner
    CouplingHighpass voicingHighpass;    // Mesa's gain-dependent voicing shelf, additive (see .cpp)
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // Diezel's own tighter corners

    double deepLpAlpha = 0.0;
    double deepLpState = 0.0;

    Oversampler4x oversampler;
};
