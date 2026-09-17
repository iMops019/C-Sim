#include "MorningGloryPedal.h"

#include <cmath>

MorningGloryPedal::MorningGloryPedal() = default;

void MorningGloryPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void MorningGloryPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto gainRangeHi = std::round(gainRange.get()) >= 0.5f;
    auto boostAmount = boost.get() / 100.0f;
    auto brightCutAmount = brightCut.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setGain(gainAmount);
        stage.setGainRangeHi(gainRangeHi);
        stage.setBoost(boostAmount);
        stage.setBrightCut(brightCutAmount);

        auto* data = channelData[ch];
        stage.processBlock(data, data, numSamples);
    }

    auto outGain = volume.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> MorningGloryPedal::getParameters()
{
    return { &gain, &gainRange, &boost, &brightCut, &volume };
}
