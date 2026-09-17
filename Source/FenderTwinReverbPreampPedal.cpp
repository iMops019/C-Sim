#include "FenderTwinReverbPreampPedal.h"

namespace
{
    // A real Fender preamp has a dedicated recovery gain stage (V1B/V2B
    // in the actual Vibrato channel - see dsp/FenderTwinReverbPreamp's
    // own header) sitting right after the passive tone stack, precisely
    // because that tone stack throws away a huge amount of signal on its
    // own - a measured diagnostic found ~22dB of insertion loss from
    // FenderToneStack's default component values at this pedal's own
    // default Bass/Mid/Treble settings, confirming a user report that
    // chaining this pedal after another one sounded "full and in your
    // face" while chaining it BEFORE that same pedal sounded quiet -
    // there was nothing here compensating for that loss at all. This
    // toolkit's dsp:: cascade currently runs all of its own gain stages
    // BEFORE the tone stack (see FenderTwinReverbPreamp) rather than
    // splitting a stage in between the way the real amp does, so this
    // fixed makeup gain stands in for that missing real recovery stage.
    constexpr float toneStackRecoveryGain = 12.6f; // ~+22dB, matching the measured loss
}

FenderTwinReverbPreampPedal::FenderTwinReverbPreampPedal() = default;

void FenderTwinReverbPreampPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    currentSampleRate = sampleRate;
    preampStages.clear();
    toneStacks.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preampStages.emplace_back(sampleRate);
        toneStacks.emplace_back(sampleRate); // default Components - see header
    }

    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 5000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));
    for (auto& f : presenceFilter)
    {
        f.setCoefficients(presenceCoeffs);
        f.reset();
    }
}

void FenderTwinReverbPreampPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(preampStages.size()));

    auto gainAmount = gain.get() / 100.0f;
    auto volumeAmount = volume.get() / 100.0f;
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 5000.0, 0.707f, juce::Decibels::decibelsToGain((presence.get() / 100.0f) * 24.0f - 12.0f));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& preamp = preampStages[static_cast<size_t>(ch)];
        preamp.setGain(gainAmount);
        preamp.setVolume(volumeAmount);

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

std::vector<PedalParameter*> FenderTwinReverbPreampPedal::getParameters()
{
    return { &gain, &volume, &bass, &mid, &treble, &presence, &level };
}
