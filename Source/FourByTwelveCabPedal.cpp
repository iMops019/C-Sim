#include "FourByTwelveCabPedal.h"
#include "CabImpulseResponse.h"

FourByTwelveCabPedal::FourByTwelveCabPedal() = default;

void FourByTwelveCabPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    // Load before prepare(), as juce::dsp::Convolution recommends, so the
    // IR is guaranteed active for the very first process() call.
    convolution.loadImpulseResponse(CabImpulseResponse::generateFourByTwelveV30(sampleRate),
                                     sampleRate,
                                     juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::yes);

    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32>(juce::jmax(1, maximumBlockSize)),
        static_cast<juce::uint32>(juce::jmax(1, numChannels))
    };
    convolution.prepare(spec);
}

void FourByTwelveCabPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    juce::dsp::AudioBlock<float> block(channelData, static_cast<size_t>(numChannels), static_cast<size_t>(numSamples));
    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);

    auto gain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], gain, numSamples);
}

std::vector<PedalParameter*> FourByTwelveCabPedal::getParameters()
{
    return { &level };
}
