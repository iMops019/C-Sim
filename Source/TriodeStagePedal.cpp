#include "TriodeStagePedal.h"

TriodeStagePedal::TriodeStagePedal() = default;

void TriodeStagePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    oversamplers.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        oversamplers.emplace_back(sampleRate);
}

void TriodeStagePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto driveGain = 1.0f + (gain.get() / 100.0f) * 15.0f; // 1x-16x into the stage
    auto biasOffset = static_cast<double>(bias.get()) / 50.0; // -1..+1 volts from nominal

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(oversamplers.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto dynamicBias = stage[ch].getParameters().gridBias + biasOffset;

        oversamplers[static_cast<size_t>(ch)].processBlock(
            channelData[ch], channelData[ch], numSamples,
            [this, ch, driveGain, dynamicBias](float x)
            {
                return stage[ch].processSampleWithBias(x * driveGain, dynamicBias);
            });
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> TriodeStagePedal::getParameters()
{
    return { &gain, &bias, &level };
}
