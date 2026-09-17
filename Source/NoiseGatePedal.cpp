#include "NoiseGatePedal.h"

NoiseGatePedal::NoiseGatePedal() = default;

void NoiseGatePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    gates.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        gates.emplace_back(sampleRate);
}

void NoiseGatePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(gates.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& gate = gates[static_cast<size_t>(ch)];
        gate.setThresholdDb(threshold.get());
        gate.setReleaseMs(release.get());
        gate.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> NoiseGatePedal::getParameters()
{
    return { &threshold, &release };
}
