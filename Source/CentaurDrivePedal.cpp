#include "CentaurDrivePedal.h"

CentaurDrivePedal::CentaurDrivePedal() = default;

void CentaurDrivePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void CentaurDrivePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setDrive(drive.get() / 100.0f);
        stage.setLightDistortion(lightDistortion.get() / 100.0f);
        stage.setDepth(depth.get() / 100.0f);
        stage.setTone(tone.get() / 100.0f);
        stage.processBlock(channelData[ch], channelData[ch], numSamples);
    }

    auto outGain = volume.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> CentaurDrivePedal::getParameters()
{
    return { &lightDistortion, &depth, &drive, &tone, &volume };
}
