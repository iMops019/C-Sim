#include "MesaRectifierPreampPedal.h"

namespace
{
    float knobToGainFactor(float knobValue)
    {
        auto db = (knobValue / 100.0f) * 24.0f - 12.0f;
        return juce::Decibels::decibelsToGain(db);
    }
}

MesaRectifierPreampPedal::MesaRectifierPreampPedal()
{
    updateFilters();
}

void MesaRectifierPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;

    preampStages.clear();
    for (int ch = 0; ch < maxChannels; ++ch)
        preampStages.emplace_back(sampleRate);

    updateFilters();

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        bassFilter[ch].reset();
        midFilter[ch].reset();
        trebleFilter[ch].reset();
        presenceFilter[ch].reset();
    }
}

void MesaRectifierPreampPedal::updateFilters()
{
    auto bassCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 150.0, 0.707f, knobToGainFactor(bass.get()));
    auto midCoeffs = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 700.0, 1.0f, knobToGainFactor(mid.get()));
    auto trebleCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3000.0, 0.707f, knobToGainFactor(treble.get()));
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 4500.0, 0.707f, knobToGainFactor(presence.get()));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        bassFilter[ch].setCoefficients(bassCoeffs);
        midFilter[ch].setCoefficients(midCoeffs);
        trebleFilter[ch].setCoefficients(trebleCoeffs);
        presenceFilter[ch].setCoefficients(presenceCoeffs);
    }
}

void MesaRectifierPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateFilters();

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(preampStages.size()));
    auto gainAmount = gain.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        preampStages[static_cast<size_t>(ch)].setGain(gainAmount);
        preampStages[static_cast<size_t>(ch)].processBlock(channelData[ch], channelData[ch], numSamples);

        auto* data = channelData[ch];
        bassFilter[ch].processSamples(data, numSamples);
        midFilter[ch].processSamples(data, numSamples);
        trebleFilter[ch].processSamples(data, numSamples);
        presenceFilter[ch].processSamples(data, numSamples);
    }

    auto outputGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outputGain, numSamples);
}

std::vector<PedalParameter*> MesaRectifierPreampPedal::getParameters()
{
    return { &gain, &bass, &mid, &treble, &presence, &level };
}
