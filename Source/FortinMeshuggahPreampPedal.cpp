#include "FortinMeshuggahPreampPedal.h"

namespace
{
    // Typical published Marshall JCM800-style tone stack values - notably
    // smaller bass cap and bigger mid pot than FenderToneStack's own
    // defaults, which is most of why a Marshall reads as tighter and more
    // mid-forward than a Fender/Mesa voicing at the same knob settings.
    // Not transcribed from one specific factory schematic, same honesty
    // level as FenderToneStack's own defaults.
    FenderToneStack::Components marshallToneStackComponents()
    {
        FenderToneStack::Components c;
        c.trebleCapF = 250.0e-12;
        c.bassCapF = 0.022e-6;
        c.treblePotOhms = 250.0e3;
        c.bassPotOhms = 1.0e6;
        c.midPotOhms = 25.0e3;
        c.slopeResistorOhms = 33.0e3;
        return c;
    }
}

FortinMeshuggahPreampPedal::FortinMeshuggahPreampPedal() = default;

void FortinMeshuggahPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate, marshallToneStackComponents());
    }
}

void FortinMeshuggahPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gain1Amount = gain1.get() / 100.0f;
    auto gain2Amount = gain2.get() / 100.0f;
    auto masterAmount = master.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& preamp = preampStages[static_cast<size_t>(ch)];
        preamp.setGain1(gain1Amount);
        preamp.setGain2(gain2Amount);
        preamp.setMaster(masterAmount);

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

std::vector<PedalParameter*> FortinMeshuggahPreampPedal::getParameters()
{
    return { &gain1, &gain2, &master, &bass, &mid, &treble, &level };
}
