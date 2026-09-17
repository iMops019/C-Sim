#include "DistortionPedal.h"

DistortionPedal::DistortionPedal() = default;

void DistortionPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    currentSampleRate = sampleRate;

    oversamplers.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        oversamplers.emplace_back(sampleRate);
}

void DistortionPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    // Hotter range than the Lab's raw Diode Clipper (1x-10x) - this is
    // meant to be a complete, ready-to-play distortion, not a subtle
    // building-block blend.
    auto driveGain = 1.0f + (gain.get() / 100.0f) * 14.0f;
    auto toneHz = juce::jmap(tone.get() / 100.0f, 800.0f, 9000.0f);

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(oversamplers.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        oversamplers[static_cast<size_t>(ch)].processBlock(
            channelData[ch], channelData[ch], numSamples,
            [this, ch, driveGain](float x) { return clipper[ch].processSample(x * driveGain); });

        toneFilters[ch].setCoefficients(juce::IIRCoefficients::makeLowPass(currentSampleRate, toneHz));
        toneFilters[ch].processSamples(channelData[ch], numSamples);
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> DistortionPedal::getParameters()
{
    return { &gain, &tone, &level };
}
