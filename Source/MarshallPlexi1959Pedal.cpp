#include "MarshallPlexi1959Pedal.h"

#include "SpeakerLoadLink.h"

#include <cmath>
#include <cstring>

MarshallPlexi1959Pedal::MarshallPlexi1959Pedal() = default;

void MarshallPlexi1959Pedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused(maximumBlockSize);

    amps.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
        amps.push_back(std::make_unique<MarshallPlexi1959Amp>(sampleRate));
}

void MarshallPlexi1959Pedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(amps.size()));

    // Which speaker is on the end of this amp: the Cabinet's, if one is in the
    // chain and processing; otherwise the generic default. A read that collided
    // with the Cabinet's write keeps what we had.
    AmpSpeakerLoad::Speaker linked;
    switch (SpeakerLoadLink::read(linked))
    {
        case SpeakerLoadLink::ReadResult::ok:   drivenSpeaker = linked; break;
        case SpeakerLoadLink::ReadResult::none: drivenSpeaker = AmpSpeakerLoad::Speaker {}; break;
        case SpeakerLoadLink::ReadResult::busy: break;
    }

    using Routing = MarshallPlexiPreamp::Routing;
    auto routing = channel.get() >= 1.5f ? Routing::Jumpered : (channel.get() >= 0.5f ? Routing::ChannelII : Routing::ChannelI);
    auto pickupKind = pickup.get() >= 1.5f ? MarshallPlexi1959Amp::Pickup::Humbucker
                                           : (pickup.get() >= 0.5f ? MarshallPlexi1959Amp::Pickup::SingleCoil : MarshallPlexi1959Amp::Pickup::Off);
    auto tapOhms = impedance.get() >= 1.5f ? 16.0 : (impedance.get() >= 0.5f ? 8.0 : 4.0);

    // A mono guitar is normally duplicated onto both channels. The amp is expensive
    // and deterministic, so when the two input channels are bit-identical it is run
    // once and the result copied: the same audio as running it twice.
    auto identicalStereo = numChannelsToProcess == 2
                           && std::memcmp(channelData[0], channelData[1], sizeof(float) * static_cast<size_t>(numSamples)) == 0;

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto* data = channelData[ch];
        if (ch == 1 && identicalStereo)
        {
            juce::FloatVectorOperations::copy(data, channelData[0], numSamples);
            continue;
        }

        auto& amp = *amps[static_cast<size_t>(ch)];

        amp.setRouting(routing);
        amp.setLowInput(input.get() >= 0.5f);
        amp.setPickup(pickupKind);
        amp.setVolumeI(volumeI.get() / 100.0f);
        amp.setVolumeII(volumeII.get() / 100.0f);
        amp.setTreble(treble.get() / 100.0f);
        amp.setMiddle(middle.get() / 100.0f);
        amp.setBass(bass.get() / 100.0f);
        amp.setPresence(presence.get() / 100.0f);
        amp.setImpedanceTap(tapOhms);
        amp.setSpeaker(drivenSpeaker);

        // Guitar volts in, speaker volts out, then to the rack's normalised level.
        juce::FloatVectorOperations::multiply(data, guitarVoltsPerUnit, numSamples);
        amp.processBlock(data, data, numSamples);
        juce::FloatVectorOperations::multiply(data, 1.0f / speakerFullScaleVolts, numSamples);

        // Safety ceiling, the same idiom as the toolkit's other amp pedals:
        // transparent at normal levels, only catches an extreme combination of
        // knobs before it would hard-clip digitally.
        for (int n = 0; n < numSamples; ++n)
            data[n] = std::tanh(data[n]);
    }
}

std::vector<PedalParameter*> MarshallPlexi1959Pedal::getParameters()
{
    return { &channel, &input, &pickup, &volumeI, &volumeII, &treble, &middle, &bass, &presence, &impedance };
}
