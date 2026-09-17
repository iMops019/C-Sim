#include "MesaDiezelHybridPreampPedal.h"

namespace
{
    // Same researched values as DiezelVH4PreampPedal - see that file's
    // own comment for where these come from.
    FenderToneStack::Components hybridToneStackComponents()
    {
        FenderToneStack::Components c;
        c.trebleCapF = 680.0e-12;
        c.bassCapF = 22.0e-9;
        c.treblePotOhms = 250.0e3;
        c.bassPotOhms = 1.0e6;
        c.midPotOhms = 25.0e3;
        c.slopeResistorOhms = 39.0e3;
        return c;
    }
}

MesaDiezelHybridPreampPedal::MesaDiezelHybridPreampPedal() = default;

void MesaDiezelHybridPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate, hybridToneStackComponents());
    }
}

void MesaDiezelHybridPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto deepAmount = deep.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& preamp = preampStages[static_cast<size_t>(ch)];
        preamp.setGain(gainAmount);
        preamp.setDeep(deepAmount);

        auto* data = channelData[ch];
        preamp.processBlock(data, data, numSamples);

        auto& toneStack = toneStacks[static_cast<size_t>(ch)];
        toneStack.setControls(treble.get() / 100.0f, bass.get() / 100.0f, mid.get() / 100.0f);
        for (int i = 0; i < numSamples; ++i)
            data[i] = toneStack.processSample(data[i]);
    }

    auto outputGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outputGain, numSamples);
}

std::vector<PedalParameter*> MesaDiezelHybridPreampPedal::getParameters()
{
    return { &gain, &deep, &bass, &mid, &treble, &level };
}
