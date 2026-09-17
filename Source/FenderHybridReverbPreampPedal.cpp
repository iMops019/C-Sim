#include "FenderHybridReverbPreampPedal.h"

namespace
{
    // A real Fender preamp has a dedicated recovery gain stage right
    // after the passive tone stack, precisely because that tone stack
    // throws away a huge amount of signal on its own - a measured
    // diagnostic on FenderTwinReverbPreampPedal (the same tone stack
    // reused here) found ~22dB of insertion loss at typical knob
    // settings, with nothing compensating for it. See that pedal's own
    // comment for the full explanation - applies identically here.
    constexpr float toneStackRecoveryGain = 12.6f; // ~+22dB, matching the measured loss
}

FenderHybridReverbPreampPedal::FenderHybridReverbPreampPedal() = default;

void FenderHybridReverbPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    currentSampleRate = sampleRate;
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate); // default Components - same blackface values as both parents
    }

    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 5000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));
    for (auto& f : presenceFilter)
    {
        f.setCoefficients(presenceCoeffs);
        f.reset();
    }
}

void FenderHybridReverbPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto volumeAmount = volume.get() / 100.0f;
    auto breakupAmount = breakup.get() / 100.0f;
    auto growlAmount = growl.get() / 100.0f;
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 5000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& preamp = preampStages[static_cast<size_t>(ch)];
        preamp.setGain(gainAmount);
        preamp.setVolume(volumeAmount);
        preamp.setBreakup(breakupAmount);
        preamp.setGrowl(growlAmount);

        auto* data = channelData[ch];
        preamp.processBlock(data, data, numSamples);

        auto& toneStack = toneStacks[static_cast<size_t>(ch)];
        toneStack.setControls(treble.get() / 100.0f, bass.get() / 100.0f, mid.get() / 100.0f);
        for (int i = 0; i < numSamples; ++i)
            data[i] = toneStack.processSample(data[i]) * toneStackRecoveryGain;

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

std::vector<PedalParameter*> FenderHybridReverbPreampPedal::getParameters()
{
    return { &gain, &volume, &breakup, &growl, &bass, &mid, &treble, &presence, &level };
}
