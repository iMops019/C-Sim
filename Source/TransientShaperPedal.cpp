#include "TransientShaperPedal.h"

#include <cmath>

namespace
{
    float timeToCoeff(float timeSeconds, float sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (timeSeconds * sampleRate));
    }

    float followEnvelope(float current, float target, float attackCoeff, float releaseCoeff)
    {
        auto coeff = target > current ? attackCoeff : releaseCoeff;
        return current + coeff * (target - current);
    }
}

TransientShaperPedal::TransientShaperPedal() = default;

void TransientShaperPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;

    auto sr = static_cast<float>(sampleRate);
    fastAttackCoeff = timeToCoeff(0.0005f, sr);   // 0.5ms - tracks transients almost instantly
    fastReleaseCoeff = timeToCoeff(0.005f, sr);   // 5ms
    slowAttackCoeff = timeToCoeff(0.010f, sr);    // 10ms - lags behind, represents settled level
    slowReleaseCoeff = timeToCoeff(0.200f, sr);   // 200ms

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        fastEnvelope[ch] = 0.0f;
        slowEnvelope[ch] = 0.0f;
    }
}

void TransientShaperPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto attackDbRange = attack.get() / 100.0f * 12.0f;
    auto sustainDbRange = sustain.get() / 100.0f * 12.0f;

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            auto rectified = std::abs(data[i]);

            fastEnvelope[ch] = followEnvelope(fastEnvelope[ch], rectified, fastAttackCoeff, fastReleaseCoeff);
            slowEnvelope[ch] = followEnvelope(slowEnvelope[ch], rectified, slowAttackCoeff, slowReleaseCoeff);

            auto reference = juce::jmax(0.0001f, slowEnvelope[ch]);
            auto transientRatio = juce::jlimit(0.0f, 1.0f, (fastEnvelope[ch] - slowEnvelope[ch]) / reference);
            auto sustainRatio = juce::jlimit(0.0f, 1.0f, juce::jmin(fastEnvelope[ch], slowEnvelope[ch]) / reference);

            auto gainDb = attackDbRange * transientRatio + sustainDbRange * sustainRatio;
            data[i] *= juce::Decibels::decibelsToGain(gainDb);
        }
    }
}

std::vector<PedalParameter*> TransientShaperPedal::getParameters()
{
    return { &attack, &sustain };
}
