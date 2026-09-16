#include "ToneStackPedal.h"

ToneStackPedal::ToneStackPedal() = default;

void ToneStackPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stacks.emplace_back(sampleRate);
}

void ToneStackPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stacks.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        stacks[static_cast<size_t>(ch)].setControls(treble.get() / 100.0f, bass.get() / 100.0f, mid.get() / 100.0f);

        auto* data = channelData[ch];
        for (int i = 0; i < numSamples; ++i)
            data[i] = stacks[static_cast<size_t>(ch)].processSample(data[i]);
    }
}

std::vector<PedalParameter*> ToneStackPedal::getParameters()
{
    return { &bass, &mid, &treble };
}
