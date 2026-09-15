#include "FourByTwelveCabPedal.h"

FourByTwelveCabPedal::FourByTwelveCabPedal()
{
    updateFilters();
}

void FourByTwelveCabPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;
    updateFilters();

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCut[ch].reset();
        bodyResonance[ch].reset();
        presencePeak[ch].reset();
        highCut1[ch].reset();
        highCut2[ch].reset();
    }
}

void FourByTwelveCabPedal::updateFilters()
{
    // A 4x12 doesn't reproduce much below here.
    auto lowCutCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, 90.0);

    // Cab/speaker body thump.
    auto bodyCoeffs = juce::IIRCoefficients::makePeakFilter(
        currentSampleRate, 120.0, 1.2f, juce::Decibels::decibelsToGain(3.0f));

    // Vintage 30's characteristic upper-mid bite.
    auto presenceCoeffs = juce::IIRCoefficients::makePeakFilter(
        currentSampleRate, 2800.0, 1.0f, juce::Decibels::decibelsToGain(4.0f));

    // Two cascaded low-pass stages for a steeper, more speaker-like
    // top-end roll-off than a single filter gives.
    auto highCut1Coeffs = juce::IIRCoefficients::makeLowPass(currentSampleRate, 5500.0, 0.707f);
    auto highCut2Coeffs = juce::IIRCoefficients::makeLowPass(currentSampleRate, 6500.0, 1.0f);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCut[ch].setCoefficients(lowCutCoeffs);
        bodyResonance[ch].setCoefficients(bodyCoeffs);
        presencePeak[ch].setCoefficients(presenceCoeffs);
        highCut1[ch].setCoefficients(highCut1Coeffs);
        highCut2[ch].setCoefficients(highCut2Coeffs);
    }
}

void FourByTwelveCabPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateFilters();

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        lowCut[ch].processSamples(data, numSamples);
        bodyResonance[ch].processSamples(data, numSamples);
        presencePeak[ch].processSamples(data, numSamples);
        highCut1[ch].processSamples(data, numSamples);
        highCut2[ch].processSamples(data, numSamples);
    }

    auto gain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], gain, numSamples);
}

std::vector<PedalParameter*> FourByTwelveCabPedal::getParameters()
{
    return { &level };
}
