#include "CabImpulseResponse.h"

#include <cmath>

namespace CabImpulseResponse
{

namespace
{
    struct ResonantMode
    {
        float frequencyHz;
        float amplitude;
        float decaySeconds;
    };

    // Frequencies/amplitudes chosen to land where a V30-loaded 4x12
    // actually resonates: a low body mode, a couple of cabinet/cone
    // modes through the mids, and the characteristic upper-mid bite.
    constexpr ResonantMode modes[] = {
        { 100.0f,  0.90f, 0.055f },
        { 180.0f,  0.55f, 0.045f },
        { 400.0f,  0.35f, 0.035f },
        { 800.0f,  0.28f, 0.025f },
        { 1200.0f, 0.22f, 0.020f },
        { 2500.0f, 0.38f, 0.016f },
        { 3200.0f, 0.30f, 0.013f },
        { 4500.0f, 0.14f, 0.009f },
    };

    void renderChannel(float* data, int numSamples, double sampleRate, float phaseOffsetRadians, int sampleDelay)
    {
        for (int n = sampleDelay; n < numSamples; ++n)
        {
            auto t = static_cast<float>(n - sampleDelay) / static_cast<float>(sampleRate);
            float value = 0.0f;

            for (auto& mode : modes)
            {
                auto phase = juce::MathConstants<float>::twoPi * mode.frequencyHz * t + phaseOffsetRadians;
                value += mode.amplitude * std::sin(phase) * std::exp(-t / mode.decaySeconds);
            }

            data[n] = value;
        }

        // Sharp initial transient (cone/speaker attack) on top of the modes.
        auto attackSamples = juce::jmin(numSamples - sampleDelay, static_cast<int>(0.001 * sampleRate));
        for (int n = 0; n < attackSamples; ++n)
        {
            auto idx = sampleDelay + n;
            auto envelope = 1.0f - (static_cast<float>(n) / static_cast<float>(attackSamples));
            data[idx] += envelope * envelope;
        }

        // Fade the tail out so truncating the IR doesn't click.
        auto fadeSamples = juce::jmin(numSamples / 10, static_cast<int>(0.01 * sampleRate));
        for (int i = 0; i < fadeSamples; ++i)
        {
            auto idx = numSamples - fadeSamples + i;
            if (idx < 0 || idx >= numSamples)
                continue;

            auto gain = 1.0f - (static_cast<float>(i) / static_cast<float>(fadeSamples));
            data[idx] *= gain;
        }
    }
}

juce::AudioBuffer<float> generateFourByTwelveV30(double sampleRate)
{
    constexpr double durationSeconds = 0.12;
    auto numSamples = juce::jmax(64, static_cast<int>(sampleRate * durationSeconds));

    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();

    // Two near-identical channels with a small delay/phase offset between
    // them, approximating blending two mic positions for stereo width.
    renderChannel(buffer.getWritePointer(0), numSamples, sampleRate, 0.0f, 0);
    renderChannel(buffer.getWritePointer(1), numSamples, sampleRate, 0.35f,
                  static_cast<int>(0.0003 * sampleRate));

    // Tame sub-bass build-up and harsh top end from the raw synthesis
    // before it becomes the cab's fixed frequency response.
    juce::IIRFilter lowCut, highCut;
    lowCut.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 80.0));
    highCut.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, 5500.0, 0.9f));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        lowCut.processSamples(data, numSamples);
        highCut.processSamples(data, numSamples);
        lowCut.reset();
        highCut.reset();
    }

    buffer.applyGain(0.9f / juce::jmax(0.0001f, buffer.getMagnitude(0, numSamples)));

    return buffer;
}

}
