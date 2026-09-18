#include "CabinetPedal.h"
#include "CabinetVisualEditor.h"

#include <cmath>

CabinetPedal::CabinetPedal() = default;

void CabinetPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = juce::jmax(1, maximumBlockSize);
    currentNumChannels = juce::jmax(1, numChannels);

    scratchA.setSize(currentNumChannels, currentMaxBlockSize);
    scratchB.setSize(currentNumChannels, currentMaxBlockSize);
    scratchRoom.setSize(currentNumChannels, currentMaxBlockSize);

    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32>(currentMaxBlockSize),
        static_cast<juce::uint32>(currentNumChannels)
    };
    convA.prepare(spec);
    convB.prepare(spec);
    convRoom.prepare(spec);

    // Forces a fresh IR load for everything on the first process() call
    // below, since these can't match any real setting.
    lastCab = lastMicTypeA = lastMicTypeB = lastLiveStudio = -1;
    lastMicPositionAStep = lastMicPositionBStep = -1;

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCutFilters[ch].reset();
        highCutFilters[ch].reset();
        resonanceFilters[ch].reset();
        airFilters[ch].reset();
    }
}

void CabinetPedal::reloadMicIRIfNeeded(int mic, bool cabChanged)
{
    auto isA = (mic == 0);
    auto& typeParam = isA ? micTypeA : micTypeB;
    auto& positionParam = isA ? micPositionA : micPositionB;
    auto& lastType = isA ? lastMicTypeA : lastMicTypeB;
    auto& lastPositionStep = isA ? lastMicPositionAStep : lastMicPositionBStep;

    auto type = static_cast<int>(std::round(typeParam.get()));
    auto positionStep = static_cast<int>(std::round(positionParam.get() / 5.0f)); // 5% steps - bounds reload frequency during a knob drag

    if (! cabChanged && type == lastType && positionStep == lastPositionStep)
        return;

    lastType = type;
    lastPositionStep = positionStep;

    // Touching Mic A's own Type/Position knobs resynthesizes it, ending
    // the one-shot loaded-IR override (see loadImpulseResponseFile) - so
    // the makeup gain that only applies to a real loaded file (see
    // process()) needs to switch off here too, not just when a new file
    // is loaded.
    if (isA)
        usingLoadedIR = false;

    auto cabType = static_cast<int>(std::round(cab.get()));
    auto& conv = isA ? convA : convB;

    // loadImpulseResponse() is wait-free and documented safe to call while
    // process() runs elsewhere, same as the file-loading path below - so
    // this can hot-swap live from a knob turn with no audio glitch beyond
    // the brief cost of resynthesizing and re-partitioning this one IR.
    conv.loadImpulseResponse(
        CabImpulseResponse::generateMicIR(currentSampleRate, cabType, type, static_cast<float>(positionStep) * 0.05f),
        currentSampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
}

void CabinetPedal::reloadRoomIRIfNeeded(bool liveStudioChanged)
{
    if (! liveStudioChanged)
        return;

    auto isStudio = std::round(liveStudio.get()) >= 0.5f;

    convRoom.loadImpulseResponse(
        CabImpulseResponse::generateRoomIR(currentSampleRate, isStudio),
        currentSampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
}

bool CabinetPedal::loadImpulseResponseFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    // Overrides Mic A with a real captured IR. Note this is a one-shot
    // override, not persisted state: turning Mic A's own Type/Position
    // knobs afterward will resynthesize and replace it, same as touching
    // any other setting that's baked into an IR.
    auto maxSamples = static_cast<size_t>(currentSampleRate * 2.0);

    convA.loadImpulseResponse(file,
                               juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::yes,
                               maxSamples,
                               juce::dsp::Convolution::Normalise::yes);

    // Mic Blend/Room still mix in Mic B's and the room engine's synthetic
    // IRs, which have no phase relationship whatsoever to a real captured
    // one - blending them in (the defaults are 30%/15%) produces comb
    // filtering/phasiness against the real IR, not a pleasant "two mics"
    // blend. Reset both to 0 so loading a real IR is heard cleanly by
    // default, the same way the old single-cab pedal always sounded -
    // both knobs stay live afterward if the user wants to experiment with
    // mixing synthetic character back in on purpose.
    micBlend.set(0.0f);
    roomAmount.set(0.0f);

    // A real captured IR measures much darker above ~3-4kHz than the
    // synthetic cabs' own already-tuned voicing (see Air's own comment
    // in the header) - bumped higher here specifically, rather than
    // just raising Air's overall default, so the synthetic cabs' sound
    // doesn't change for anyone not touching this feature at all.
    air.set(45.0f);

    // Makeup gain for the loaded file itself - see the comment on
    // usingLoadedIR/process() for why this exists alongside
    // Normalise::yes above, not instead of it.
    usingLoadedIR = true;

    return true;
}

void CabinetPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto cabType = static_cast<int>(std::round(cab.get()));
    auto cabChanged = (cabType != lastCab);
    lastCab = cabType;

    reloadMicIRIfNeeded(0, cabChanged);
    reloadMicIRIfNeeded(1, cabChanged);

    auto liveStudioType = static_cast<int>(std::round(liveStudio.get()));
    auto liveStudioChanged = (liveStudioType != lastLiveStudio);
    lastLiveStudio = liveStudioType;
    reloadRoomIRIfNeeded(liveStudioChanged);

    auto channelsToUse = juce::jmin(numChannels, currentNumChannels, maxChannels);

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        scratchA.copyFrom(ch, 0, channelData[ch], numSamples);
        scratchB.copyFrom(ch, 0, channelData[ch], numSamples);
        scratchRoom.copyFrom(ch, 0, channelData[ch], numSamples);
    }

    {
        juce::dsp::AudioBlock<float> block(scratchA.getArrayOfWritePointers(), static_cast<size_t>(channelsToUse), static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> context(block);
        convA.process(context);

        // Makeup gain for a real, user-loaded IR file specifically (not
        // the synthetic cabs, which already normalise their own peak
        // level - see generateMicIR's own applyGain call). JUCE's
        // Convolution::Normalise::yes (passed on every load in this
        // file) is a one-time scale of the IR itself against a fixed
        // sum-of-squared-magnitude target, not a runtime match to the
        // dry signal's own level - a real commercial cab IR, especially
        // a bright/thin close-mic capture, can still land quieter after
        // that than a synthetic cab's own tuned output. Standard
        // commercial IR loaders compensate with roughly +6 to +12dB of
        // fixed makeup gain on top of whatever their own normalisation
        // does; +9dB (the middle of that range) applied here only while
        // Mic A is actually a loaded file matches that convention
        // without touching the Level knob's own meaning for everyone
        // else.
        if (usingLoadedIR)
        {
            constexpr float loadedIRMakeupGainDb = 9.0f;
            block.multiplyBy(juce::Decibels::decibelsToGain(loadedIRMakeupGainDb));
        }
    }
    {
        juce::dsp::AudioBlock<float> block(scratchB.getArrayOfWritePointers(), static_cast<size_t>(channelsToUse), static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> context(block);
        convB.process(context);
    }
    {
        juce::dsp::AudioBlock<float> block(scratchRoom.getArrayOfWritePointers(), static_cast<size_t>(channelsToUse), static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> context(block);
        convRoom.process(context);
    }

    auto blend = micBlend.get() / 100.0f;     // 0 = all Mic A, 1 = all Mic B
    auto roomMix = roomAmount.get() / 100.0f; // 0 = no room mic, 1 = all room mic

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        auto* out = channelData[ch];
        auto* a = scratchA.getReadPointer(ch);
        auto* b = scratchB.getReadPointer(ch);
        auto* room = scratchRoom.getReadPointer(ch);

        for (int n = 0; n < numSamples; ++n)
        {
            auto micSignal = a[n] * (1.0f - blend) + b[n] * blend;
            out[n] = micSignal * (1.0f - roomMix) + room[n] * roomMix;
        }
    }

    // Low Cut/High Cut/Resonance trim - recomputed every block, same as
    // ToneStackPedal's per-block setControls(); cheap relative to the
    // convolution above and lets these three knobs move continuously.
    auto lowCutCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate, lowCut.get());
    auto highCutCoeffs = juce::IIRCoefficients::makeLowPass(currentSampleRate, highCut.get(), 0.7f);
    auto resonanceCoeffs = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 120.0, 1.0,
                                                                  juce::Decibels::decibelsToGain(resonance.get()));

    // Boost-only "air" shelf, applied last so it can brighten back up
    // past whatever High Cut trimmed - see Air's own comment. 0..100 maps
    // to 0..+15dB, a stronger max than Presence-style controls elsewhere
    // in this app since its whole job is compensating for how dark a real
    // captured IR measures, not just a taste tweak.
    auto airDb = (air.get() / 100.0f) * 15.0f;
    auto airCoeffs = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 5000.0, 0.707f,
                                                            juce::Decibels::decibelsToGain(airDb));

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        lowCutFilters[ch].setCoefficients(lowCutCoeffs);
        highCutFilters[ch].setCoefficients(highCutCoeffs);
        resonanceFilters[ch].setCoefficients(resonanceCoeffs);
        airFilters[ch].setCoefficients(airCoeffs);

        auto* data = channelData[ch];
        lowCutFilters[ch].processSamples(data, numSamples);
        highCutFilters[ch].processSamples(data, numSamples);
        resonanceFilters[ch].processSamples(data, numSamples);
        airFilters[ch].processSamples(data, numSamples);
    }

    // Width (mid-side) - only meaningful with a genuine stereo pair.
    if (channelsToUse >= 2)
    {
        auto widthFactor = width.get() / 100.0f; // 100 = unchanged, 0 = mono, 200 = extra wide
        auto* left = channelData[0];
        auto* right = channelData[1];

        for (int n = 0; n < numSamples; ++n)
        {
            auto mid = 0.5f * (left[n] + right[n]);
            auto side = 0.5f * (left[n] - right[n]) * widthFactor;
            left[n] = mid + side;
            right[n] = mid - side;
        }
    }

    auto outGain = level.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> CabinetPedal::getParameters()
{
    return { &cab, &micTypeA, &micPositionA, &micTypeB, &micPositionB, &micBlend,
             &liveStudio, &roomAmount, &lowCut, &highCut, &resonance, &air, &width, &level };
}

std::unique_ptr<juce::Component> CabinetPedal::createCustomEditor()
{
    return std::make_unique<CabinetVisualEditor>(*this, cab, micTypeA, micPositionA, micTypeB, micPositionB, micBlend);
}

std::vector<PedalParameter*> CabinetPedal::getCustomEditorHandledParameters()
{
    return { &cab, &micTypeA, &micPositionA, &micTypeB, &micPositionB };
}
