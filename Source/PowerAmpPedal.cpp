#include "PowerAmpPedal.h"

#include <cmath>

PowerAmpPedal::PowerAmpPedal() = default;

void PowerAmpPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    tubeStages.clear();
    solidStateStages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        tubeStages.emplace_back(sampleRate);
        solidStateStages.emplace_back(sampleRate);
    }
}

void PowerAmpPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto isSolidState = std::round(type.get()) >= 0.5f;
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(tubeStages.size()));
    auto driveAmount = drive.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        if (isSolidState)
        {
            solidStateStages[static_cast<size_t>(ch)].setDrive(driveAmount);
            solidStateStages[static_cast<size_t>(ch)].processBlock(channelData[ch], channelData[ch], numSamples);
        }
        else
        {
            tubeStages[static_cast<size_t>(ch)].setDrive(driveAmount);
            tubeStages[static_cast<size_t>(ch)].setSag(sag.get() / 100.0f);
            tubeStages[static_cast<size_t>(ch)].setFeedback(feedback.get() / 100.0f);
            tubeStages[static_cast<size_t>(ch)].processBlock(channelData[ch], channelData[ch], numSamples);
        }
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> PowerAmpPedal::getParameters()
{
    return { &type, &drive, &sag, &feedback, &level };
}
