#include "FuzzChorusPedal.h"

FuzzChorusPedal::FuzzChorusPedal() = default;

void FuzzChorusPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    fuzzStages.clear();
    chorusStages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        fuzzStages.emplace_back(sampleRate);
        chorusStages.emplace_back(sampleRate);
    }
}

void FuzzChorusPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(fuzzStages.size()));

    auto fuzzAmount = fuzz.get() / 100.0f;
    auto chorusAmount = chorus.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        auto& fuzzStage = fuzzStages[static_cast<size_t>(ch)];
        fuzzStage.setFuzz(fuzzAmount);
        fuzzStage.processBlock(data, data, numSamples);

        auto& chorusStage = chorusStages[static_cast<size_t>(ch)];
        chorusStage.setChorus(chorusAmount);
        chorusStage.processBlock(data, data, numSamples);
    }
}

std::vector<PedalParameter*> FuzzChorusPedal::getParameters()
{
    return { &fuzz, &chorus };
}
