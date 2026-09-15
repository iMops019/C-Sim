#include "ReverbPedal.h"

ReverbPedal::ReverbPedal()
{
    updateReverbParameters();
}

void ReverbPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    reverb.setSampleRate(sampleRate);
    reverb.reset();
}

void ReverbPedal::updateReverbParameters()
{
    auto mixAmount = mix.get() / 100.0f;

    juce::Reverb::Parameters params;
    params.roomSize = roomSize.get() / 100.0f;
    params.damping = damping.get() / 100.0f;
    params.wetLevel = mixAmount;
    params.dryLevel = 1.0f - mixAmount;
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);
}

void ReverbPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateReverbParameters();

    if (numChannels >= 2)
        reverb.processStereo(channelData[0], channelData[1], numSamples);
    else if (numChannels == 1)
        reverb.processMono(channelData[0], numSamples);

    auto gain = level.get() / 100.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::multiply(channelData[channel], gain, numSamples);
}

std::vector<PedalParameter*> ReverbPedal::getParameters()
{
    return { &mix, &roomSize, &damping, &level };
}
