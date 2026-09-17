#include "GraphicEQPedal.h"

GraphicEQPedal::GraphicEQPedal() = default;

void GraphicEQPedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    eqs.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        eqs.emplace_back(sampleRate);
}

void GraphicEQPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(eqs.size()));

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& eq = eqs[static_cast<size_t>(ch)];
        eq.setBandGainDb(0, band100.get());
        eq.setBandGainDb(1, band170.get());
        eq.setBandGainDb(2, band280.get());
        eq.setBandGainDb(3, band460.get());
        eq.setBandGainDb(4, band770.get());
        eq.setBandGainDb(5, band1300.get());
        eq.setBandGainDb(6, band2200.get());
        eq.setBandGainDb(7, band3600.get());
        eq.setBandGainDb(8, band6000.get());
        eq.setBandGainDb(9, band10000.get());

        eq.processBlock(channelData[ch], channelData[ch], numSamples);
    }
}

std::vector<PedalParameter*> GraphicEQPedal::getParameters()
{
    return { &band100, &band170, &band280, &band460, &band770,
             &band1300, &band2200, &band3600, &band6000, &band10000 };
}
