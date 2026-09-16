#include "HybridAmp.h"

#include <algorithm>
#include <vector>

namespace
{
    // Same makeup-gain reasoning as EmoAmpFullChainTest: a passive tone
    // stack has real insertion loss, and a real amp has recovery/driver
    // gain stages a bare module chain otherwise lacks. Without these the
    // signal reaching the power amp is too quiet for sag to do anything.
    constexpr float toneStackMakeupGain = 3.0f;
    constexpr float driverStageMakeupGain = 2.0f;

    float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
}

HybridAmp::HybridAmp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      cleanStage(sampleRateToUse),
      metalStage(sampleRateToUse),
      gate(sampleRateToUse),
      toneStack(sampleRateToUse),
      reverb(sampleRateToUse),
      tremolo(sampleRateToUse),
      powerAmp(sampleRateToUse)
{
    cleanStage.setSensitivity(0.7f);
    cleanStage.setBaseDrive(0.1f);

    metalStage.setDrive(1.0f);
    metalStage.setDiodeBlend(0.3f);

    gate.setThresholdDb(-48.0f);
    gate.setReleaseMs(130.0f);

    toneStack.setControls(0.6f, 0.5f, 0.55f);

    reverb.setDecay(0.35f);
    reverb.setMix(0.2f);

    tremolo.setRateHz(5.0f);
    tremolo.setDepth(0.3f);

    powerAmp.setFeedback(0.35f);
    powerAmp.setSag(twinStyleSag);
}

void HybridAmp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    cleanStage.setSampleRate(sampleRate);
    metalStage.setSampleRate(sampleRate);
    gate.setSampleRate(sampleRate);
    toneStack.setSampleRate(sampleRate);
    reverb.setSampleRate(sampleRate);
    tremolo.setSampleRate(sampleRate);
    powerAmp.setSampleRate(sampleRate);
    reset();
}

void HybridAmp::setBlend(float amount)
{
    blend = std::clamp(amount, 0.0f, 1.0f);
    powerAmp.setSag(lerp(twinStyleSag, deluxeMetalSag, blend));
}

void HybridAmp::reset()
{
    cleanStage.reset();
    metalStage.reset();
    gate.reset();
    toneStack.reset();
    reverb.reset();
    tremolo.reset();
}

void HybridAmp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> cleanOut(static_cast<size_t>(numSamples));
    cleanStage.processBlock(input, cleanOut.data(), numSamples);

    // Pad between the two stages before the metal cascade applies its own
    // (independently-tuned) drive multiplier on top. KorenTriodeStage's
    // grid-conduction side has no hard ceiling (a known simplification -
    // real grid conduction clamps it, ours doesn't), so cascading one
    // already-hot gain stage's output straight into another's aggressive
    // drive compounds past what either stage was tuned/tested against -
    // confirmed the hard way: HybridAmpTest hit real NaN/Inf at blend>0
    // and high input levels until this clamp was added. Each build's own
    // tests never caught this because neither ever fed one module's
    // output into another module's independent gain stage.
    std::vector<float> paddedForMetal(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        paddedForMetal[static_cast<size_t>(n)] = std::clamp(cleanOut[static_cast<size_t>(n)], -1.5f, 1.5f);

    std::vector<float> metalOut(static_cast<size_t>(numSamples));
    metalStage.processBlock(paddedForMetal.data(), metalOut.data(), numSamples);

    std::vector<float> blended(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        blended[static_cast<size_t>(n)] = lerp(cleanOut[static_cast<size_t>(n)], metalOut[static_cast<size_t>(n)], blend);

    std::vector<float> gated(static_cast<size_t>(numSamples));
    gate.processBlock(blended.data(), gated.data(), numSamples);

    for (int n = 0; n < numSamples; ++n)
        gated[static_cast<size_t>(n)] = toneStack.processSample(gated[static_cast<size_t>(n)]) * toneStackMakeupGain;

    std::vector<float> reverbed(static_cast<size_t>(numSamples));
    reverb.processBlock(gated.data(), reverbed.data(), numSamples);

    std::vector<float> tremoloed(static_cast<size_t>(numSamples));
    tremolo.processBlock(reverbed.data(), tremoloed.data(), numSamples);

    for (int n = 0; n < numSamples; ++n)
        tremoloed[static_cast<size_t>(n)] *= driverStageMakeupGain;

    powerAmp.processBlock(tremoloed.data(), output, numSamples);
}
