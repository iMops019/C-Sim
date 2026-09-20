#include "HoofPedal.h"

#include <cstring>

HoofPedal::HoofPedal() = default;

void HoofPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void HoofPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    // A mono guitar is normally duplicated onto both channels. The circuit is expensive and
    // deterministic, so when the two input channels are bit-identical it is run once and
    // the result copied: the same audio as running it twice.
    auto identicalStereo = numChannelsToProcess == 2
                           && std::memcmp(channelData[0], channelData[1], sizeof(float) * static_cast<size_t>(numSamples)) == 0;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];
        if (ch == 1 && identicalStereo)
        {
            juce::FloatVectorOperations::copy(data, channelData[0], numSamples);
            continue;
        }

        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setFuzz(fuzz.get() / 100.0f);
        stage.setTone(tone.get() / 100.0f);
        stage.setShift(shift.get() / 100.0f);
        stage.setLevel(level.get() / 100.0f);
        stage.processBlock(data, data, numSamples);
    }
}

std::vector<PedalParameter*> HoofPedal::getParameters()
{
    return { &fuzz, &tone, &level, &shift };
}
