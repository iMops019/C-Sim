#include "FourByTwelveCabPedal.h"
#include "CabImpulseResponse.h"

FourByTwelveCabPedal::FourByTwelveCabPedal() = default;

void FourByTwelveCabPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;

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

bool FourByTwelveCabPedal::loadImpulseResponseFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    // loadImpulseResponse() is wait-free and safe to call while process()
    // is running on the audio thread elsewhere (per its own docs), so this
    // can hot-swap the IR live from a UI button click with no audio glitch.
    // Cap at 2 seconds - real cab IRs are short; this just guards against
    // an accidentally huge file (e.g. a full reverb IR) tanking CPU/latency.
    auto maxSamples = static_cast<size_t>(currentSampleRate * 2.0);

    convolution.loadImpulseResponse(file,
                                     juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::yes,
                                     maxSamples,
                                     juce::dsp::Convolution::Normalise::yes);
    return true;
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
