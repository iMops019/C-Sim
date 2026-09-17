#include "WardenPedal.h"

WardenPedal::WardenPedal() = default;

void WardenPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        stages.emplace_back(sampleRate);
}

void WardenPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    auto sustainAmount = sustain.get() / 100.0f;
    auto ratioAmount = ratio.get() / 100.0f;
    auto attackAmount = attack.get() / 100.0f;
    auto releaseAmount = release.get() / 100.0f;
    auto toneAmount = tone.get() / 100.0f;
    auto levelAmount = level.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setSustain(sustainAmount);
        stage.setRatio(ratioAmount);
        stage.setAttack(attackAmount);
        stage.setRelease(releaseAmount);
        stage.setTone(toneAmount);
        stage.setLevel(levelAmount);

        auto* data = channelData[ch];
        stage.processBlock(data, data, numSamples);
    }
}

std::vector<PedalParameter*> WardenPedal::getParameters()
{
    return { &sustain, &ratio, &attack, &release, &tone, &level };
}
