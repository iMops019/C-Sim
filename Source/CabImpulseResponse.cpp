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

    // Retuned for a tighter, more percussive "chug" character: shorter
    // decay times throughout (less smear/boom), the low body mode pulled
    // back so it doesn't dominate, and the 2.5-4kHz presence region pushed
    // up so pick attack and upper-mid bite cut through instead of getting
    // buried - that upper-mid energy is what was reading as "muffled".
    constexpr ResonantMode modes[] = {
        { 100.0f,  0.55f, 0.030f },
        { 180.0f,  0.35f, 0.025f },
        { 400.0f,  0.25f, 0.020f },
        { 800.0f,  0.22f, 0.016f },
        { 1200.0f, 0.20f, 0.014f },
        { 2500.0f, 0.55f, 0.012f },
        { 3200.0f, 0.50f, 0.010f },
        { 4000.0f, 0.35f, 0.008f },
        { 4800.0f, 0.22f, 0.006f },
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

        // Sharp initial transient (cone/speaker attack) on top of the modes -
        // boosted a bit so pick attack stays percussive rather than smeared.
        auto attackSamples = juce::jmin(numSamples - sampleDelay, static_cast<int>(0.001 * sampleRate));
        for (int n = 0; n < attackSamples; ++n)
        {
            auto idx = sampleDelay + n;
            auto envelope = 1.0f - (static_cast<float>(n) / static_cast<float>(attackSamples));
            data[idx] += 1.3f * envelope * envelope;
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
    // Shorter than before (was 0.12s) - a tight rhythm cab shouldn't ring
    // on for long after the transient.
    constexpr double durationSeconds = 0.08;
    auto numSamples = juce::jmax(64, static_cast<int>(sampleRate * durationSeconds));

    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();

    // Two near-identical channels with a small delay/phase offset between
    // them, approximating blending two mic positions for stereo width.
    renderChannel(buffer.getWritePointer(0), numSamples, sampleRate, 0.0f, 0);
    renderChannel(buffer.getWritePointer(1), numSamples, sampleRate, 0.35f,
                  static_cast<int>(0.0003 * sampleRate));

    // Tame sub-bass build-up before it becomes the cab's fixed frequency
    // response. Low cut raised slightly (was 80Hz) and the high cut opened
    // up and de-resonated (was 5500Hz/Q0.9) so presence/bite survives
    // instead of getting damped into a dull, muffled top end.
    juce::IIRFilter lowCut, highCut;
    lowCut.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 90.0));
    highCut.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, 6500.0, 0.7f));

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
