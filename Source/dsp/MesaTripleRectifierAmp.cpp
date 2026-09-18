#include "MesaTripleRectifierAmp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    constexpr double preGainCutoffHz = 95.0; // input stage's own cathode-bypass rolloff

    // Bright-cap sweep on the 250k gain pot, traced from the schematic
    // analysis: ~656Hz at low Gain up to ~1.4kHz at high Gain.
    constexpr double voicingCutoffLowGainHz = 656.0;
    constexpr double voicingCutoffHighGainHz = 1400.0;

    // Feeding stage 2: an ordinary interstage corner. Feeding stage 3:
    // tighter, standing in for that stage's missing cathode bypass cap
    // (see header).
    constexpr double interStage1CutoffHz = 40.0;
    constexpr double interStage2CutoffHz = 120.0;
}

MesaTripleRectifierAmp::MesaTripleRectifierAmp(double sampleRateToUse)
    : preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse),
      sampleRate(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);

    // Progressively lighter drive per stage - three identical
    // nonlinearities in a row would reinforce the same harmonic content
    // rather than building on it.
    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 7.0;
    p2.inputToGridVolts = 5.0;
    p3.inputToGridVolts = 3.5;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);

    updateVoicingFilter();
}

void MesaTripleRectifierAmp::setGain(float amount) noexcept
{
    gain = clamp01(amount);
    updateVoicingFilter();
}

void MesaTripleRectifierAmp::setVolume(float amount) noexcept
{
    volume = clamp01(amount);
}

void MesaTripleRectifierAmp::updateVoicingFilter()
{
    voicingCutoffHz = static_cast<float>(voicingCutoffLowGainHz
                                          + (voicingCutoffHighGainHz - voicingCutoffLowGainHz) * gain);
    voicingHighpass.setCutoff(sampleRate * 4.0, voicingCutoffHz);
}

float MesaTripleRectifierAmp::getVoicingCutoffHz() const noexcept
{
    return voicingCutoffHz;
}

void MesaTripleRectifierAmp::reset() noexcept
{
    preGainHighpass.reset();
    voicingHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    masterClipper.reset();
    oversampler.reset();
}

float MesaTripleRectifierAmp::processOversampledSample(float x) noexcept
{
    // Each Koren stage's own small-signal gain near its bias point is
    // well under unity - a fixed makeup multiplier between stages stands
    // in for a real triode stage's actual voltage gain.
    //
    // History: an earlier version bounded each interstage with a plain
    // tanh() to fix a real blowup bug in the originally-recovered
    // constants (see git history / project memory for the full story).
    // That was stable but left the cascade sounding "clean and dull" per
    // direct user feedback - each KorenTriodeStage re-normalises to its
    // own unity point regardless of incoming amplitude, so bounding every
    // interstage to roughly +-1 meant later stages never actually got
    // pushed into their own more-saturating region. Fixed by moving the
    // safety net to the END instead of gating every interstage: a real
    // `DiodeClipperStage` master stage (same technique this toolkit's
    // FortinMeshuggahPreamp already uses) after the cascade. A diode
    // clipper's node voltage physically stays within a diode drop
    // regardless of how hard it's driven (see DiodeClipperStage.h), so
    // it's inherently self-limiting - the tube cascade can compound
    // freely again (real distortion buildup, matching the originally-
    // researched 16x drive / 3x interstage makeup) while the diode
    // master alone guarantees a bounded, safe output. Verified via a
    // throwaway diagnostic (deleted after use): crest factor dropped
    // from ~2.1-2.9 (dull/dynamic) to ~1.2 (genuinely "smashed", on par
    // with FortinMeshuggahPreamp's own ~1.30), and worst-case peak at
    // hot input + max Gain is now ~1.7-1.8 pre-final-safety-tanh, not
    // the 100+ the unbounded tube-only cascade produced.
    constexpr float interStageMakeupGain = 3.0f;

    // A shunt voltage divider attenuates below its corner rather than
    // blocking it outright - floored at 60% rather than toward 0, unlike
    // the true series coupling caps elsewhere in this cascade.
    constexpr float voicingFloor = 0.6f;

    auto driveGain = 1.0f + gain * 15.0f; // 1x to 16x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = voicingFloor * signal + (1.0f - voicingFloor) * voicingHighpass.processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    signal = stages[2].processSample(signal);

    // The real hard-clipping master stage, driven harder as Gain rises -
    // see header comment.
    auto masterDrive = 1.0f + gain * 4.0f;
    return masterClipper.processSample(signal * masterDrive);
}

void MesaTripleRectifierAmp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });

    for (int n = 0; n < numSamples; ++n)
        output[n] *= volume;
}
