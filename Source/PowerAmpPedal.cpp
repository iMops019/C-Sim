#include "PowerAmpPedal.h"

PowerAmpPedal::PowerAmpPedal() = default;

void PowerAmpPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void PowerAmpPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        stages[static_cast<size_t>(ch)].setSag(sag.get() / 100.0f);
        stages[static_cast<size_t>(ch)].setFeedback(feedback.get() / 100.0f);
        stages[static_cast<size_t>(ch)].processBlock(channelData[ch], channelData[ch], numSamples);
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> PowerAmpPedal::getParameters()
{
    return { &sag, &feedback, &level };
}
