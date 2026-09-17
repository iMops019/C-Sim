#include "SpringReverbPedal.h"

SpringReverbPedal::SpringReverbPedal() = default;

void SpringReverbPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    reverbs.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        reverbs.emplace_back(sampleRate);
}

void SpringReverbPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(reverbs.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& reverb = reverbs[static_cast<size_t>(ch)];
        reverb.setDecay(decay.get() / 100.0f);
        reverb.setMix(mix.get() / 100.0f);
        reverb.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> SpringReverbPedal::getParameters()
{
    return { &decay, &mix };
}
