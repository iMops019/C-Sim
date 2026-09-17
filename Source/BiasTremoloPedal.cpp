#include "BiasTremoloPedal.h"

BiasTremoloPedal::BiasTremoloPedal() = default;

void BiasTremoloPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    tremolos.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        tremolos.emplace_back(sampleRate);
}

void BiasTremoloPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(tremolos.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& tremolo = tremolos[static_cast<size_t>(ch)];
        tremolo.setRateHz(rate.get());
        tremolo.setDepth(depth.get() / 100.0f);
        tremolo.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> BiasTremoloPedal::getParameters()
{
    return { &rate, &depth };
}
