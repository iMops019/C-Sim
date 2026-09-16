#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "DiodeClipperStage.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// The metal build's core preamp: 3 cascaded Koren-model gain stages with
// inter-stage coupling high-passes, wrapped in 4x oversampling. Per the
// build order's metal-build requirements:
//  - A pre-gain high-pass (~100Hz, before the whole cascade) is the
//    single biggest lever for tightness on dropped tunings - it runs
//    once at the base rate since it isn't itself a source of new
//    harmonic content that would need anti-aliasing.
//  - Each stage gets its own light inter-stage filtering (60Hz, run
//    inside the oversampled block since it sits between two nonlinear
//    stages) rather than one nonlinearity doing all the work.
//  - The three stages are deliberately given different drive amounts
//    rather than being identical copies, closer to how real cascaded
//    stages differ and avoiding the reinforced sameness three identical
//    nonlinearities in a row would produce.
//  - A diode clipper is blended in after the tube cascade (Mesa
//    Rectifier-style "extra edge") - its hard, symmetric knee is a
//    distinctly different character from the tubes' soft/asymmetric
//    curve, and setDiodeBlend controls how much of it mixes in.
class MetalPreampChain
{
public:
    explicit MetalPreampChain(double sampleRate);

    void setSampleRate(double newSampleRate);

    // 0..1: scales how hard the cascade is driven overall.
    void setDrive(float newDrive);

    // 0..1: how much of the diode clipper's harder edge mixes in after
    // the tube cascade. 0 = pure tube, 1 = fully replaced by diode edge.
    void setDiodeBlend(float newBlend);

    void reset();

    // Not yet optimised for zero-allocation real-time use (allocates a
    // scratch buffer per call) - matches Oversampler4x's own current
    // verification-first scope.
    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 3;

    double sampleRate;
    float drive = 0.6f;
    float diodeBlend = 0.0f;

    CouplingHighpass preGainHighpass;
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass;
    DiodeClipperStage diodeClipper;

    Oversampler4x oversampler;
};
