#include "PalmMuteTamerPedal.h"

PalmMuteTamerPedal::PalmMuteTamerPedal() = default;

void PalmMuteTamerPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    tamers.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        tamers.emplace_back(sampleRate);
}

void PalmMuteTamerPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(tamers.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& tamer = tamers[static_cast<size_t>(ch)];
        tamer.setAmount(amount.get() / 100.0f);
        tamer.setSensitivity(sensitivity.get() / 100.0f);
        tamer.processBlock(channelData[ch], channelData[ch], numSamples);
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> PalmMuteTamerPedal::getParameters()
{
    return { &amount, &sensitivity, &level };
}
