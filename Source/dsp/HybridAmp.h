#pragma once

#include "BiasModulatedTremolo.h"
#include "DynamicGainStage.h"
#include "FenderToneStack.h"
#include "MetalPreampChain.h"
#include "NoiseGate.h"
#include "PowerAmpStage.h"
#include "SpringReverb.h"

// Step 9: the hybrid build, combining specific stages from the metal and
// emo builds rather than blending two fully-separate final mixes. The
// signal path:
//
//   input -> DynamicGainStage (always active - the emo build's
//            touch-responsive edge-of-breakup front end)
//         -> [blend crossfade: clean path vs. MetalPreampChain cascade]
//         -> NoiseGate (harmless on a clean signal, tames hiss when
//            blended toward the high-gain side)
//         -> FenderToneStack (the one topology both builds share)
//         -> SpringReverb -> BiasModulatedTremolo (carried over as
//            universal texture - genuinely characterful even on a
//            heavier blend, and central to the genre's identity either way)
//         -> PowerAmpStage (sag interpolates from Twin-style headroom
//            toward Deluxe/metal-style compression as blend increases)
//
// This serves a real musical need rather than being wiring for its own
// sake: midwest emo/math rock songs routinely alternate shimmery clean
// sections with heavier, tighter distorted ones within the same song -
// one continuous Blend knob spans that range instead of two disconnected
// amps.
class HybridAmp
{
public:
    explicit HybridAmp(double sampleRate);

    void setSampleRate(double newSampleRate);

    // 0 = full clean/emo character, 1 = full pushed/metal character.
    void setBlend(float amount);

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    double sampleRate;
    float blend = 0.3f;

    DynamicGainStage cleanStage;
    MetalPreampChain metalStage;
    NoiseGate gate;
    FenderToneStack toneStack;
    SpringReverb reverb;
    BiasModulatedTremolo tremolo;
    PowerAmpStage powerAmp;

    static constexpr float twinStyleSag = 0.15f;
    // Matches the emo build's own Deluxe-style sag value (see
    // EmoAmpFullChainTest) for consistency. Note: a harmonic-content dip
    // at intermediate blend values (investigated via HybridAmpTest) is
    // NOT caused by this - halving it changed the measured harmonic
    // content by <0.01%. The real cause is that parallel-blending
    // cleanOut with a further-distorted metalOut sums two differently-
    // shaped clipping curves of the same tone, which isn't monotonic in
    // a simple harmonic-content metric; see HybridAmpTest's comments.
    static constexpr float deluxeMetalSag = 0.75f;
};
