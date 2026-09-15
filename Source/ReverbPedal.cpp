#include "ReverbPedal.h"

ReverbPedal::ReverbPedal()
{
    juce::Reverb::Parameters params;
    params.roomSize = 0.5f;
    params.damping = 0.5f;
    params.wetLevel = 0.33f;
    params.dryLevel = 0.4f;
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);
}

void ReverbPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    reverb.setSampleRate(sampleRate);
    reverb.reset();
}

void ReverbPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    if (numChannels >= 2)
        reverb.processStereo(channelData[0], channelData[1], numSamples);
    else if (numChannels == 1)
        reverb.processMono(channelData[0], numSamples);
}
