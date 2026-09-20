#include "TubeScreamerPedal.h"

TubeScreamerPedal::TubeScreamerPedal(Model modelToUse) : model(modelToUse) {}

void TubeScreamerPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate, model == Model::TS808 ? TubeScreamerStage::ts808() : TubeScreamerStage::ts9());
}

void TubeScreamerPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setDrive(drive.get() / 100.0f);
        stage.setTone(tone.get() / 100.0f);
        stage.setLevel(level.get() / 100.0f);
        stage.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> TubeScreamerPedal::getParameters()
{
    return { &drive, &tone, &level };
}
