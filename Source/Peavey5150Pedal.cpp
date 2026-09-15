#include "Peavey5150Pedal.h"

#include <cmath>

namespace
{
    float knobToGainFactor(float knobValue)
    {
        auto db = (knobValue / 100.0f) * 24.0f - 12.0f;
        return juce::Decibels::decibelsToGain(db);
    }

    // Asymmetric tanh saturation - the asymmetry (small bias before the
    // curve) adds even harmonics on top of tanh's odd ones, closer to how
    // a real tube stage clips than a symmetric waveshaper.
    float asymmetricSaturate(float x, float drive, float bias)
    {
        auto biased = x * drive + bias;
        return std::tanh(biased) - std::tanh(bias);
    }
}

Peavey5150Pedal::Peavey5150Pedal()
{
    updateFilters();
}

void Peavey5150Pedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;
    updateFilters();

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        preGainHighPass[ch].reset();
        interStageHighPass[ch].reset();
        bassFilter[ch].reset();
        midFilter[ch].reset();
        trebleFilter[ch].reset();
        resonanceFilter[ch].reset();
        presenceFilter[ch].reset();
    }
}

void Peavey5150Pedal::updateFilters()
{
    // Sits just below a dropped low C (~65Hz) - tightens sub-bass without
    // eating the fundamental before it hits the gain stages.
    auto preGainCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, 55.0);

    // Between the two clipping stages - this is where most of the "tight
    // vs. flubby" character of a high-gain amp actually comes from.
    auto interStageCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, 90.0);

    auto bassCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 150.0, 0.707f, knobToGainFactor(bass.get()));
    auto midCoeffs = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 700.0, 1.0f, knobToGainFactor(mid.get()));
    auto trebleCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3000.0, 0.707f, knobToGainFactor(treble.get()));
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3500.0, 0.707f, knobToGainFactor(presence.get()));

    // Power-amp-style low end - deep and felt rather than tonal, separate
    // from the preamp Bass control above. Turn it down for tightness on
    // dropped tunings, up for a looser, boomier low end.
    auto resonanceCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 70.0, 0.707f, knobToGainFactor(resonance.get()));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        preGainHighPass[ch].setCoefficients(preGainCoeffs);
        interStageHighPass[ch].setCoefficients(interStageCoeffs);
        bassFilter[ch].setCoefficients(bassCoeffs);
        midFilter[ch].setCoefficients(midCoeffs);
        trebleFilter[ch].setCoefficients(trebleCoeffs);
        presenceFilter[ch].setCoefficients(presenceCoeffs);
        resonanceFilter[ch].setCoefficients(resonanceCoeffs);
    }
}

float Peavey5150Pedal::processSample(float x, int channel)
{
    // Stage 1: heavy drive, first clipping stage.
    auto driveAmount = 1.0f + (gain.get() / 100.0f) * 39.0f;
    auto stage1 = asymmetricSaturate(x, driveAmount, 0.15f);

    stage1 = interStageHighPass[channel].processSingleSampleRaw(stage1);

    // Stage 2: fixed moderate drive, adds compression/sustain on top.
    auto stage2 = asymmetricSaturate(stage1, 3.0f, 0.1f);

    // Cascaded tanh stages get loud fast - bring it back down before the
    // tone stack, roughly compensating for the gain knob's own boost.
    return stage2 / (1.0f + (gain.get() / 100.0f) * 2.5f);
}

void Peavey5150Pedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateFilters();

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        preGainHighPass[ch].processSamples(data, numSamples);

        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample(data[i], ch);

        bassFilter[ch].processSamples(data, numSamples);
        midFilter[ch].processSamples(data, numSamples);
        trebleFilter[ch].processSamples(data, numSamples);
        resonanceFilter[ch].processSamples(data, numSamples);
        presenceFilter[ch].processSamples(data, numSamples);
    }

    auto outputGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outputGain, numSamples);
}

std::vector<PedalParameter*> Peavey5150Pedal::getParameters()
{
    return { &gain, &bass, &mid, &treble, &presence, &resonance, &level };
}
