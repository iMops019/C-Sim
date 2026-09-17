#include "FenderPrincetonReverbPreampPedal.h"

namespace
{
    // No Mid pot on the real amp - see header - a fixed mid-loading
    // value stands in for the fixed resistor in that spot.
    constexpr float fixedMidAmount = 0.4f;

    // A real Fender preamp has a dedicated recovery gain stage right
    // after the passive tone stack, precisely because that tone stack
    // throws away a huge amount of signal on its own - a measured
    // diagnostic on FenderTwinReverbPreampPedal (the same tone stack
    // reused here) found ~22dB of insertion loss at typical knob
    // settings, with nothing compensating for it. See that pedal's own
    // comment for the full explanation - applies identically here.
    constexpr float toneStackRecoveryGain = 12.6f; // ~+22dB, matching the measured loss
}

FenderPrincetonReverbPreampPedal::FenderPrincetonReverbPreampPedal() = default;

void FenderPrincetonReverbPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    currentSampleRate = sampleRate;
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate); // default Components, same blackface values as the Twin
    }
}

void FenderPrincetonReverbPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto volumeAmount = volume.get() / 100.0f;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& preamp = preampStages[static_cast<size_t>(ch)];
        preamp.setGain(gainAmount);
        preamp.setVolume(volumeAmount);

        auto* data = channelData[ch];
        preamp.processBlock(data, data, numSamples);

        auto& toneStack = toneStacks[static_cast<size_t>(ch)];
        toneStack.setControls(treble.get() / 100.0f, bass.get() / 100.0f, fixedMidAmount);
        for (int i = 0; i < numSamples; ++i)
            data[i] = toneStack.processSample(data[i]) * toneStackRecoveryGain;
    }

    auto outputGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outputGain, numSamples);
}

std::vector<PedalParameter*> FenderPrincetonReverbPreampPedal::getParameters()
{
    return { &gain, &volume, &bass, &treble, &level };
}
