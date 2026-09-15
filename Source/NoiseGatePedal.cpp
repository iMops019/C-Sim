#include "NoiseGatePedal.h"

#include <cmath>

namespace
{
    float timeToCoeff(float timeSeconds, float sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (timeSeconds * sampleRate));
    }
}

NoiseGatePedal::NoiseGatePedal() = default;

void NoiseGatePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        envelope[ch] = 0.0f;
        gateGain[ch] = 1.0f;
    }
}

void NoiseGatePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto thresholdGain = juce::Decibels::decibelsToGain(threshold.get());

    auto sr = static_cast<float>(currentSampleRate);
    auto envCoeff = timeToCoeff(0.005f, sr);       // ~5ms envelope smoothing
    auto gateUpCoeff = timeToCoeff(0.002f, sr);    // fast open, ~2ms
    auto gateDownCoeff = timeToCoeff(release.get() / 1000.0f, sr);

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            auto rectified = std::abs(data[i]);
            envelope[ch] += envCoeff * (rectified - envelope[ch]);

            auto targetGain = envelope[ch] >= thresholdGain ? 1.0f : 0.0f;
            auto coeff = targetGain > gateGain[ch] ? gateUpCoeff : gateDownCoeff;
            gateGain[ch] += coeff * (targetGain - gateGain[ch]);

            data[i] *= gateGain[ch];
        }
    }
}

std::vector<PedalParameter*> NoiseGatePedal::getParameters()
{
    return { &threshold, &release };
}
