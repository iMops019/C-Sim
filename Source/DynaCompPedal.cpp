#include "DynaCompPedal.h"

DynaCompPedal::DynaCompPedal() = default;

void DynaCompPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void DynaCompPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    auto sensitivityAmount = sensitivity.get() / 100.0f;
    auto outputAmount = output.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setSensitivity(sensitivityAmount);
        stage.setOutput(outputAmount);

        auto* data = channelData[ch];
        stage.processBlock(data, data, numSamples);
    }
}

std::vector<PedalParameter*> DynaCompPedal::getParameters()
{
    return { &sensitivity, &output };
}
