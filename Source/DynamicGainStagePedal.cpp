#include "DynamicGainStagePedal.h"

DynamicGainStagePedal::DynamicGainStagePedal() = default;

void DynamicGainStagePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void DynamicGainStagePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setSensitivity(sensitivity.get() / 100.0f);
        stage.setBaseDrive(drive.get() / 100.0f);
        stage.processBlock(channelData[ch], channelData[ch], numSamples);
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> DynamicGainStagePedal::getParameters()
{
    return { &sensitivity, &drive, &level };
}
