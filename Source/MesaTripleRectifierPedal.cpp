#include "MesaTripleRectifierPedal.h"

#include <cmath>

MesaTripleRectifierPedal::MesaTripleRectifierPedal() = default;

void MesaTripleRectifierPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused(maximumBlockSize);

    currentSampleRate = sampleRate;

    amps.clear();
    orangeAmps.clear();
    powerAmps.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        amps.push_back(std::make_unique<MesaTripleRectifierAmp>(sampleRate));
        orangeAmps.push_back(std::make_unique<OrangeDualTerrorPreamp>(sampleRate));
        powerAmps.push_back(std::make_unique<PowerAmpStage>(sampleRate));
    }

    for (auto& filter : loadFilters)
        filter.prepare(sampleRate);
}

void MesaTripleRectifierPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(amps.size()));

    // Rectifier Select + Bold/Spongy: continuously interpolated sag - see
    // header comment for the real, sourced switches this stands in for.
    auto sagAmount = juce::jmap(low.get() / 100.0f, tightSagAmount, looseSagAmount);

    // Mid: a peaking EQ at the real, cross-sourced ~500Hz scoop center -
    // see header comment.
    auto midDb = juce::jmap(mid.get() / 100.0f, midCutDb, midBoostDb);
    auto midCoeffs = juce::IIRCoefficients::makePeakFilter(
        currentSampleRate, midFrequencyHz, 0.7, juce::Decibels::decibelsToGain(midDb));

    // High: a high-shelf at the real, sourced ~2.2kHz Baxandall corner -
    // see header comment.
    auto highDb = juce::jmap(high.get() / 100.0f, highCutDb, highBoostDb);
    auto highCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, highFrequencyHz, 0.707, juce::Decibels::decibelsToGain(highDb));

    // Depth: a boost-only low-shelf at the real, sourced 80Hz center -
    // see header comment.
    auto depthDb = (depth.get() / 100.0f) * depthBoostDb;
    auto depthCoeffs = juce::IIRCoefficients::makeLowShelf(
        currentSampleRate, depthFrequencyHz, 0.707, juce::Decibels::decibelsToGain(depthDb));

    // OR60 add-on, only used while it's On.
    auto orOn = orSwitch.get() >= 0.5f;

    // Which speaker is on the end of this amp: the Cabinet's, if one is in
    // the chain and processing; otherwise the generic default. A read that
    // collided with the Cabinet's write keeps what we had.
    if (orOn)
    {
        AmpSpeakerLoad::Speaker linked;
        switch (SpeakerLoadLink::read(linked))
        {
            case SpeakerLoadLink::ReadResult::ok:   drivenSpeaker = linked; break;
            case SpeakerLoadLink::ReadResult::none: drivenSpeaker = AmpSpeakerLoad::Speaker {}; break;
            case SpeakerLoadLink::ReadResult::busy: break;
        }
    }

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        // OR60 add-on, in front of the Mesa. Skipped entirely while Off,
        // so the Mesa path below is untouched. While Off its filter state
        // goes stale, so flipping it On mid-note can click once - fine
        // for an experimental toggle.
        if (orOn)
        {
            auto& orange = *orangeAmps[static_cast<size_t>(ch)];
            orange.setChannel(orChannel.get() >= 0.5f ? OrangeDualTerrorPreamp::Channel::Fat
                                                      : OrangeDualTerrorPreamp::Channel::TinyTerror);
            orange.setGain(orGain.get() / 100.0f);
            orange.setTone(orTone.get() / 100.0f);
            orange.setVolume(orVolume.get() / 100.0f);
            orange.setBright(juce::roundToInt(orBright.get()));
            orange.processBlock(channelData[ch], channelData[ch], numSamples);
        }

        auto& amp = *amps[static_cast<size_t>(ch)];
        amp.setGain(gain.get() / 100.0f);
        amp.setVolume(volume.get() / 100.0f);
        amp.processBlock(channelData[ch], channelData[ch], numSamples);

        midFilters[ch].setCoefficients(midCoeffs);
        midFilters[ch].processSamples(channelData[ch], numSamples);

        highFilters[ch].setCoefficients(highCoeffs);
        highFilters[ch].processSamples(channelData[ch], numSamples);

        depthFilters[ch].setCoefficients(depthCoeffs);
        depthFilters[ch].processSamples(channelData[ch], numSamples);

        auto& powerAmp = *powerAmps[static_cast<size_t>(ch)];
        powerAmp.setSag(sagAmount);
        powerAmp.setDrive(0.0f); // already gain-staged by Gain/Volume above
        powerAmp.processBlock(channelData[ch], channelData[ch], numSamples);

        // Safety ceiling: same idiom as FenderStyleAmpPedal's own final
        // tanh - transparent at normal playing levels, only catches
        // extreme knob combinations (high Gain/Volume feeding straight
        // into the power amp's own nonlinearity) before they'd otherwise
        // hard-clip into harsh digital distortion.
        for (int n = 0; n < numSamples; ++n)
            channelData[ch][n] = std::tanh(channelData[ch][n]);

        // OR60 Presence/Resonance: the amp's feedback loop into the
        // speaker's impedance curve - see the header comment. Skipped while
        // Off (the Mesa path is untouched); the filter's states are
        // physical, so it picks up cleanly when switched on.
        if (orOn)
        {
            auto& load = loadFilters[ch];
            load.setSpeaker(drivenSpeaker);
            load.setControls(presence.get() / 100.0f, resonance.get() / 100.0f);
            for (int n = 0; n < numSamples; ++n)
                channelData[ch][n] = load.processSample(channelData[ch][n]);
        }
    }
}

std::vector<PedalParameter*> MesaTripleRectifierPedal::getParameters()
{
    return { &gain, &volume, &low, &mid, &high, &depth,
             &orSwitch, &orChannel, &orGain, &orTone, &orVolume, &orBright, &presence, &resonance };
}
