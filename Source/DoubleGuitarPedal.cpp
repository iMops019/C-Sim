#include "DoubleGuitarPedal.h"

DoubleGuitarPedal::DoubleGuitarPedal() = default;

void DoubleGuitarPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    doublers.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        doublers.emplace_back(sampleRate);
        doublers.back().setLfoPhaseOffset(ch == 1 ? 0.25f : 0.0f);
    }
}

void DoubleGuitarPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(doublers.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& doubler = doublers[static_cast<size_t>(ch)];
        doubler.setDetune(detune.get() / 100.0f);
        doubler.setRateHz(rate.get());
        doubler.setMix(mix.get() / 100.0f);
        doubler.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> DoubleGuitarPedal::getParameters()
{
    return { &detune, &rate, &mix };
}
