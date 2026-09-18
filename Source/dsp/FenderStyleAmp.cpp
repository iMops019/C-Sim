#include "FenderStyleAmp.h"

namespace
{
    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // Real, sourced V1->V2 interstage coupling cap (0.02uF) into the real
    // 1M-ohm grid leak resistor - see header. 1/(2*pi*1e6*0.02e-6) ~= 8Hz.
    constexpr double interStageCouplingHz = 8.0;

    // Stage 1 (V1, bypassed cathode - full, undegenerated gain) keeps the
    // default 12AX7-fit grid swing this seed amp already used as its one
    // stage. Stage 2 (V2, unbypassed cathode) gets a smaller grid swing,
    // standing in for the real local-negative-feedback cathode
    // degeneration that measurably reduces that stage's own gain/adds
    // headroom (see header) - the same "later cascade stage gets a
    // smaller inputToGridVolts to stay more headroomy" pattern already
    // established in FenderTwinReverbPreamp.
    constexpr double stage1GridVolts = 6.0;
    constexpr double stage2GridVolts = 3.0;
}

FenderStyleAmp::FenderStyleAmp(double sampleRate)
    : interStageHighpass(sampleRate * 4.0, interStageCouplingHz),
      oversampler(sampleRate)
{
    KorenTriodeStage::Parameters p1, p2;
    p1.inputToGridVolts = stage1GridVolts;
    p2.inputToGridVolts = stage2GridVolts;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
}

void FenderStyleAmp::setGain(float amount) noexcept
{
    gain = clamp01(amount);
}

void FenderStyleAmp::setVolume(float amount) noexcept
{
    volume = clamp01(amount);
}

void FenderStyleAmp::setMu(float mu) noexcept
{
    for (auto& stage : stages)
    {
        auto params = stage.getParameters();
        if (params.mu != static_cast<double>(mu))
        {
            params.mu = static_cast<double>(mu);
            stage.setParameters(params);
        }
    }
}

void FenderStyleAmp::reset() noexcept
{
    interStageHighpass.reset();
    oversampler.reset();
}

float FenderStyleAmp::processOversampledSample(float x) noexcept
{
    // Deliberately modest, matching the project's own researched Fender
    // preamps (FenderTwinReverbPreamp etc: 1x-1.85x per stage) rather than
    // the Lab tab's raw TriodeStagePedal (1x-16x) - that range is meant
    // for a DIY building block the user chains with their own downstream
    // taming (a Diode Clipper, a deliberately pulled-down Level knob);
    // this seed amp has no such safety valve between here and the tone
    // stack/power amp, and KorenTriodeStage only self-normalises to unit
    // output AT unit input - driving it 16x past that doesn't clamp back
    // down, it just keeps growing, which is a real bug this project hit
    // (measured via a throwaway diagnostic: a realistic hard-picked power
    // chord came out the triode stage at ~14x its input peak).
    auto driveGain = 1.0f + gain * 5.0f; // 1x-6x into the first stage - a bit wider than the single-stage version's 1x-4x, since the second (headroom-adding) stage absorbs some of the extra drive rather than letting it run away
    float signal = x * driveGain;

    // V1: bypassed cathode, full gain stage.
    signal = stages[0].processSample(signal);

    // The real, sourced V1->V2 coupling cap - see header. Its corner
    // (~8Hz) sits well below the audio band, so this is close to
    // transparent - included because a real number came directly out of
    // the schematic, not because it audibly shapes anything here.
    signal = interStageHighpass.processSample(signal);

    // V2: unbypassed cathode - the real local-feedback-driven gain
    // reduction/headroom increase, standing in via a smaller grid swing.
    signal = stages[1].processSample(signal);

    // Two cascaded self-normalising stages compound gain; bring the
    // level back down before this returns to the tone stack/power amp,
    // same idiom as FenderTwinReverbPreamp's own end-of-cascade divide.
    return signal / (1.4f + gain * 0.6f);
}

void FenderStyleAmp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });

    for (int n = 0; n < numSamples; ++n)
        output[n] *= volume;
}
