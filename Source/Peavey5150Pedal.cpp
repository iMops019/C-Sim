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

    float timeToCoeff(float timeSeconds, float sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (timeSeconds * sampleRate));
    }
}

Peavey5150Pedal::Peavey5150Pedal()
{
    updateFilters();
}

void Peavey5150Pedal::prepare(double sampleRate, int maximumBlockSize, int /*numChannels*/)
{
    currentSampleRate = sampleRate;

    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(maxChannels), static_cast<size_t>(oversamplingFactor),
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    oversampling->initProcessing(static_cast<size_t>(juce::jmax(1, maximumBlockSize)));
    oversampling->reset();

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
        sagEnvelope[ch] = 0.0f;
    }
}

void Peavey5150Pedal::updateFilters()
{
    // Sits just below a dropped low C (~65Hz) - tightens sub-bass without
    // eating the fundamental before it hits the gain stages.
    auto preGainCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, 55.0);

    // Between the two clipping stages - this is where most of the "tight
    // vs. flubby" character of a high-gain amp actually comes from. Runs at
    // the oversampled rate since it operates inside the up-sampled block.
    auto oversampledRate = currentSampleRate * static_cast<double>(1 << oversamplingFactor);
    auto interStageCoeffs = juce::IIRCoefficients::makeHighPass(oversampledRate, 90.0);

    auto bassCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 150.0, 0.707f, knobToGainFactor(bass.get()));
    auto midCoeffs = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 700.0, 1.0f, knobToGainFactor(mid.get()));
    auto trebleCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3000.0, 0.707f, knobToGainFactor(treble.get()));
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3500.0, 0.707f, knobToGainFactor(presence.get()));

    // Power-amp-style low end - deep and felt rather than tonal, separate
    // from the preamp Bass control above. Turn it down for tightness on
    // dropped tunings, up for a looser, boomier low end.
    auto resonanceCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 70.0, 0.707f, knobToGainFactor(resonance.get()));

    // Sag envelope runs inside the oversampled loop, so its time constants
    // need to be in oversampled samples too. Fast-ish attack (the supply
    // droops quickly under a transient), release scaled by the Sag knob -
    // more Sag means a slower recovery, i.e. a more pronounced "breathing".
    sagAttackCoeff = timeToCoeff(0.015f, static_cast<float>(oversampledRate));
    auto sagReleaseSeconds = 0.05f + (sag.get() / 100.0f) * 0.45f;
    sagReleaseCoeff = timeToCoeff(sagReleaseSeconds, static_cast<float>(oversampledRate));

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

    // Power-amp sag: track how hard stage 2 is about to be hit (feed-
    // forward from stage 1's already-computed output, so no one-sample
    // delay is needed the way a true feedback loop would require), then
    // shift stage 2's OWN bias point by that envelope instead of just
    // ducking the output's volume afterward. A higher bias pushes the
    // tanh curve further from center - both compressing harder and
    // adding more even-harmonic asymmetry as the "rail" sags - which is
    // the actual squishy, breaking-up character real tube sag has under
    // a hard pick attack, not just a quieter signal.
    auto rectified = std::abs(stage1);
    auto sagCoeff = rectified > sagEnvelope[channel] ? sagAttackCoeff : sagReleaseCoeff;
    sagEnvelope[channel] += sagCoeff * (rectified - sagEnvelope[channel]);

    auto sagAmount = sag.get() / 100.0f;
    auto dynamicBias = 0.1f + sagAmount * sagEnvelope[channel] * 0.6f;

    // Stage 2: fixed moderate drive, adds compression/sustain on top.
    auto stage2 = asymmetricSaturate(stage1, 3.0f, dynamicBias);

    // Cascaded tanh stages get loud fast - bring it back down before the
    // tone stack, roughly compensating for the gain knob's own boost.
    return stage2 / (1.0f + (gain.get() / 100.0f) * 2.5f);
}

void Peavey5150Pedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateFilters();

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
        preGainHighPass[ch].processSamples(channelData[ch], numSamples);

    if (oversampling != nullptr)
    {
        juce::dsp::AudioBlock<float> block(channelData, static_cast<size_t>(numChannelsToProcess),
                                            static_cast<size_t>(numSamples));

        // Saturation runs at 4x sample rate so the harmonics it generates
        // above the original Nyquist get filtered out on the way back down
        // instead of aliasing.
        auto oversampledBlock = oversampling->processSamplesUp(block);
        auto osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());

        for (int ch = 0; ch < numChannelsToProcess; ++ch)
        {
            auto* data = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));

            for (int i = 0; i < osNumSamples; ++i)
                data[i] = processSample(data[i], ch);
        }

        oversampling->processSamplesDown(block);
    }

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

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
    return { &gain, &bass, &mid, &treble, &presence, &resonance, &sag, &level };
}
