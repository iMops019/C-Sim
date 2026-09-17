#include "DiezelVH4PreampPedal.h"

namespace
{
    // Values traced from a published pedal clone of the VH4's actual
    // Mega/Lead channel network (channels 3/4 specifically use the
    // larger 680pF treble cap; channel 2 uses 470pF) - see the header.
    FenderToneStack::Components diezelToneStackComponents()
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

DiezelVH4PreampPedal::DiezelVH4PreampPedal() = default;

void DiezelVH4PreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    currentSampleRate = sampleRate;
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate, diezelToneStackComponents());
    }

    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 4000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));
    for (auto& f : presenceFilter)
    {
        f.setCoefficients(presenceCoeffs);
        f.reset();
    }
}

void DiezelVH4PreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto deepAmount = deep.get() / 100.0f;
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 4000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));

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

        if (ch < 2)
        {
            presenceFilter[ch].setCoefficients(presenceCoeffs);
            presenceFilter[ch].processSamples(data, numSamples);
        }
    }

    auto outputGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outputGain, numSamples);
}

std::vector<PedalParameter*> DiezelVH4PreampPedal::getParameters()
{
    return { &gain, &deep, &bass, &mid, &treble, &presence, &level };
}
