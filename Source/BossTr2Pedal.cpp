#include "BossTr2Pedal.h"

BossTr2Pedal::BossTr2Pedal() = default;

void BossTr2Pedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void BossTr2Pedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setRate(rate.get() / 100.0f);
        stage.setDepth(depth.get() / 100.0f);
        stage.setWave(wave.get() / 100.0f);
        stage.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> BossTr2Pedal::getParameters()
{
    return { &rate, &depth, &wave };
}
