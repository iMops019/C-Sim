#include "SuperSonic22Pedal.h"

#include <cmath>

SuperSonic22Pedal::SuperSonic22Pedal() = default;

void SuperSonic22Pedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused(maximumBlockSize);

    preamps.clear();
    reverbs.clear();
    powerAmps.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        preamps.push_back(std::make_unique<SuperSonic22Preamp>(sampleRate));
        reverbs.push_back(std::make_unique<SpringReverb>(sampleRate));
        powerAmps.push_back(std::make_unique<PowerAmpStage>(sampleRate));
    }
}

void SuperSonic22Pedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(preamps.size()));

    auto burn = channel.get() >= 0.5f;
    auto fatOn = fat.get() >= 0.5f;
    auto reverbMix = (reverb.get() / 100.0f) * maxReverbMix;
    auto toPowerAmp = mixerGain / powerAmpFullScaleVolts;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];
        auto index = static_cast<size_t>(ch);

        // The preamp: guitar volts in, channel-output volts out.
        auto& preamp = *preamps[index];
        preamp.setChannel(burn ? SuperSonic22Preamp::Channel::Burn : SuperSonic22Preamp::Channel::Vintage);
        preamp.setFat(fatOn);
        preamp.setVintageVolume(vintageVolume.get() / 100.0f);
        preamp.setVintageTreble(vintageTreble.get() / 100.0f);
        preamp.setVintageBass(vintageBass.get() / 100.0f);
        preamp.setGain1(gain1.get() / 100.0f);
        preamp.setGain2(gain2.get() / 100.0f);
        preamp.setBurnTreble(burnTreble.get() / 100.0f);
        preamp.setBurnBass(burnBass.get() / 100.0f);
        preamp.setBurnMid(burnMid.get() / 100.0f);
        preamp.setBurnVolume(burnVolume.get() / 100.0f);

        juce::FloatVectorOperations::multiply(data, guitarVoltsPerUnit, numSamples);
        preamp.processBlock(data, data, numSamples);

        // Reverb is added after the channel (and before the power amp), as
        // on the amp.
        auto& spring = *reverbs[index];
        spring.setDecay(0.5f);
        spring.setMix(reverbMix);
        spring.processBlock(data, data, numSamples);

        // Volts -> the level the power stage runs at, then the stage itself.
        juce::FloatVectorOperations::multiply(data, toPowerAmp, numSamples);

        auto& powerAmp = *powerAmps[index];
        powerAmp.setSag(sagAmount);
        powerAmp.setFeedback(feedbackAmount);
        powerAmp.setDrive(0.0f); // already at the right level, from the volts above
        powerAmp.processBlock(data, data, numSamples);

        // Safety ceiling, the same idiom as the toolkit's other amp pedals:
        // transparent at normal levels, only catches an extreme combination
        // of knobs before it would hard-clip digitally.
        for (int n = 0; n < numSamples; ++n)
            data[n] = std::tanh(data[n]);
    }
}

std::vector<PedalParameter*> SuperSonic22Pedal::getParameters()
{
    return { &channel, &fat, &vintageVolume, &vintageTreble, &vintageBass,
             &gain1, &gain2, &burnTreble, &burnBass, &burnMid, &burnVolume, &reverb };
}
