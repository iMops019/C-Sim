#include "TransposePedal.h"

TransposePedal::TransposePedal() = default;

void TransposePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    shifters.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        shifters.emplace_back(sampleRate);
}

void TransposePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(shifters.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& shifter = shifters[static_cast<size_t>(ch)];
        shifter.setSemitones(semitones.get());
        shifter.setMix(mix.get() / 100.0f);
        shifter.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> TransposePedal::getParameters()
{
    return { &semitones, &mix };
}
