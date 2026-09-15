#include "CleanAmpPedal.h"

namespace
{
    // Maps a 0-100 knob to a +/-12dB gain factor, centred at 50 = 0dB.
    float knobToGainFactor(float knobValue)
    {
        auto db = (knobValue / 100.0f) * 24.0f - 12.0f;
        return juce::Decibels::decibelsToGain(db);
    }
}

CleanAmpPedal::CleanAmpPedal()
{
    updateFilters();
}

void CleanAmpPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    currentSampleRate = sampleRate;
    updateFilters();

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        rumbleFilter[ch].reset();
        bassFilter[ch].reset();
        midFilter[ch].reset();
        trebleFilter[ch].reset();
        presenceFilter[ch].reset();
    }
}

void CleanAmpPedal::updateFilters()
{
    // Rumble filter: sits well below a dropped low C (~65Hz) so it only
    // removes subsonic junk, never the guitar's own fundamental.
    auto rumbleCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, 40.0);

    auto bassCoeffs = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 100.0, 0.707f, knobToGainFactor(bass.get()));

    // Mid band centred on ~200Hz - the zone that turns boxy/muddy on
    // dropped tunings, so this knob is where drop C tone gets cleaned up.
    auto midCoeffs = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 200.0, 1.0f, knobToGainFactor(mid.get()));

    auto trebleCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 3000.0, 0.707f, knobToGainFactor(treble.get()));
    auto presenceCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 5000.0, 0.707f, knobToGainFactor(presence.get()));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        rumbleFilter[ch].setCoefficients(rumbleCoeffs);
        bassFilter[ch].setCoefficients(bassCoeffs);
        midFilter[ch].setCoefficients(midCoeffs);
        trebleFilter[ch].setCoefficients(trebleCoeffs);
        presenceFilter[ch].setCoefficients(presenceCoeffs);
    }
}

void CleanAmpPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    updateFilters();

    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];

        rumbleFilter[ch].processSamples(data, numSamples);
        bassFilter[ch].processSamples(data, numSamples);
        midFilter[ch].processSamples(data, numSamples);
        trebleFilter[ch].processSamples(data, numSamples);
        presenceFilter[ch].processSamples(data, numSamples);
    }

    auto gain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], gain, numSamples);
}

std::vector<PedalParameter*> CleanAmpPedal::getParameters()
{
    return { &bass, &mid, &treble, &presence, &level };
}
