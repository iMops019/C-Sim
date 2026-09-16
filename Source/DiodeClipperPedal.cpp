#include "DiodeClipperPedal.h"

DiodeClipperPedal::DiodeClipperPedal() = default;

void DiodeClipperPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    oversamplers.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        oversamplers.emplace_back(sampleRate);
}

void DiodeClipperPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto driveGain = 1.0f + (drive.get() / 100.0f) * 9.0f; // 1x-10x into the clipper

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(oversamplers.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        oversamplers[static_cast<size_t>(ch)].processBlock(
            channelData[ch], channelData[ch], numSamples,
            [this, ch, driveGain](float x) { return clipper[ch].processSample(x * driveGain); });
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> DiodeClipperPedal::getParameters()
{
    return { &drive, &level };
}
