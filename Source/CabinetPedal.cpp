#include "CabinetPedal.h"
#include "CabinetVisualEditor.h"
#include "dsp/DualCabMix.h"

#include <cmath>

CabinetPedal::CabinetPedal() = default;

// Stop telling any amp which speaker it's driving as soon as this Cabinet
// goes away, rather than letting it wait out the link's timeout.
CabinetPedal::~CabinetPedal()
{
    SpeakerLoadLink::clear();
}

void CabinetPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = juce::jmax(1, maximumBlockSize);
    currentNumChannels = juce::jmax(1, numChannels);

    scratchA.setSize(currentNumChannels, currentMaxBlockSize);
    scratchB.setSize(currentNumChannels, currentMaxBlockSize);
    scratchRoom.setSize(currentNumChannels, currentMaxBlockSize);
    scratchA2.setSize(currentNumChannels, currentMaxBlockSize);
    scratchB2.setSize(currentNumChannels, currentMaxBlockSize);
    pushedLeft.setSize(currentNumChannels, currentMaxBlockSize);
    pushedRight.setSize(currentNumChannels, currentMaxBlockSize);

    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32>(currentMaxBlockSize),
        static_cast<juce::uint32>(currentNumChannels)
    };
    convA.prepare(spec);
    convB.prepare(spec);
    convRoom.prepare(spec);
    convA2.prepare(spec);
    convB2.prepare(spec);

    // Forces a fresh IR load for everything on the first process() call
    // below, since these can't match any real setting.
    lastCab = lastMicTypeA = lastMicTypeB = lastLiveStudio = -1;
    lastMicPositionAStep = lastMicPositionBStep = -1;
    lastRightSigA = lastRightSigB = -1;

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCutFilters[ch].reset();
        highCutFilters[ch].reset();
        resonanceFilters[ch].reset();
        airFilters[ch].reset();
    }

    for (auto& side : speakerProcessors)
        for (auto& processor : side)
            processor.prepare(sampleRate);

    // Mic placement: the longest delay two mics can differ by is ~1.26 ms
    // (1 in vs 18 in); 3 ms of buffer leaves generous headroom.
    for (auto& state : placement)
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            state.shelf[ch].prepare(sampleRate);
            state.delay[ch].prepare(sampleRate, 0.003);
            state.delay[ch].setDelaySamples(0.0f, true);
        }
    }
}

void CabinetPedal::reloadMicIRIfNeeded(int mic, bool cabChanged)
{
    auto isA = (mic == 0);
    auto& typeParam = isA ? micTypeA : micTypeB;
    auto& positionParam = isA ? micPositionA : micPositionB;
    auto& lastType = isA ? lastMicTypeA : lastMicTypeB;
    auto& lastPositionStep = isA ? lastMicPositionAStep : lastMicPositionBStep;

    // A slot holding a real IR is left completely alone: its cab/mic/
    // position are baked into the capture, so none of those knobs apply.
    // (Pending-resynth is deliberately NOT consumed here, so it still
    // fires later if this slot is cleared.)
    if (slotLoaded[mic].load())
        return;

    auto type = static_cast<int>(std::round(typeParam.get()));
    auto positionStep = static_cast<int>(std::round(positionParam.get() / 5.0f)); // 5% steps - bounds reload frequency during a knob drag

    auto forced = pendingResynth[mic].exchange(false);
    if (! cabChanged && ! forced && type == lastType && positionStep == lastPositionStep)
        return;

    lastType = type;
    lastPositionStep = positionStep;

    auto cabType = static_cast<int>(std::round(cab.get()));
    auto secondCabType = static_cast<int>(std::round(speaker2.get())) - 1; // index 0 is "None"
    auto mixStep = static_cast<int>(std::round(speakerMix.get() / 5.0f));
    loadMicIR(isA ? convA : convB, cabType, secondCabType, mixStep, type, positionStep);
}

void CabinetPedal::loadMicIR(juce::dsp::Convolution& conv, int cabType, int secondCabType, int mixStep,
                              int micType, int positionStep)
{
    // loadImpulseResponse() is wait-free and documented safe to call while
    // process() runs elsewhere, same as the file-loading path below - so
    // this can hot-swap live from a knob turn with no audio glitch beyond
    // the brief cost of resynthesizing and re-partitioning this one IR.
    conv.loadImpulseResponse(
        CabImpulseResponse::generateMixedMicIR(currentSampleRate, cabType, secondCabType, static_cast<float>(mixStep) * 0.05f,
                                               micType, static_cast<float>(positionStep) * 0.05f),
        currentSampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
}

// Dual-cab's second cab: same mic Types/Positions as the left cab, its own
// cabinet. Only ever called while Cab R is active (see process()), so a
// user who never touches Cab R never pays for these engines. The very
// first load after Cab R is switched on takes a moment to swap in; until
// then convA2/convB2 hold whatever IR they last had (or pass audio through
// unfiltered if they've never loaded one) - a brief, one-time transient
// while dual mode engages, not something that recurs on later tweaks.
void CabinetPedal::reloadRightCabIRsIfNeeded()
{
    // Index 0 is "Same as Cab": the right side's synthetic mics then use
    // the left cab (only reachable here when a Right slot holds a real IR
    // and the other Right mic is still synthetic).
    auto cabRIndex = static_cast<int>(std::round(cabRight.get()));
    auto cabRType = cabRIndex > 0 ? cabRIndex - 1 : static_cast<int>(std::round(cab.get()));

    // "Same as Cab" mirrors the WHOLE left cab, second speaker and mix
    // included; a specific Cab R uses its own Spk 2 R / Spk Mix R.
    auto& secondParam = cabRIndex > 0 ? speaker2R : speaker2;
    auto& mixParam = cabRIndex > 0 ? speakerMixR : speakerMix;
    auto secondRType = static_cast<int>(std::round(secondParam.get())) - 1;
    // Mix only counts while there IS a second speaker (same rule as the
    // left cab's signature in process()).
    auto mixRStep = secondRType >= 0 ? static_cast<int>(std::round(mixParam.get() / 5.0f)) : 0;

    auto typeA = static_cast<int>(std::round(micTypeA.get()));
    auto typeB = static_cast<int>(std::round(micTypeB.get()));
    auto stepA = static_cast<int>(std::round(micPositionA.get() / 5.0f));
    auto stepB = static_cast<int>(std::round(micPositionB.get() / 5.0f));

    // Positions and mix are 5% steps, so at most 21 distinct values each -
    // 32 is a safe packing radix. Unique per (cab, second speaker, mix,
    // mic type, position); the second speaker is stored +1 so "none" packs
    // as 0.
    auto cabPart = (cabRType * 16 + (secondRType + 1)) * 32 + mixRStep;
    auto sigA = ((cabPart * 2 + typeA) * 32 + stepA);
    auto sigB = ((cabPart * 2 + typeB) * 32 + stepB);

    // Loaded slots are left alone (see reloadMicIRIfNeeded); a Clear on
    // an unloaded one forces a fresh synthesis even if its signature
    // happens to match what's cached.
    if (! slotLoaded[2].load())
    {
        if (pendingResynth[2].exchange(false))
            lastRightSigA = -1;
        if (sigA != lastRightSigA)
        {
            lastRightSigA = sigA;
            loadMicIR(convA2, cabRType, secondRType, mixRStep, typeA, stepA);
        }
    }
    if (! slotLoaded[3].load())
    {
        if (pendingResynth[3].exchange(false))
            lastRightSigB = -1;
        if (sigB != lastRightSigB)
        {
            lastRightSigB = sigB;
            loadMicIR(convB2, cabRType, secondRType, mixRStep, typeB, stepB);
        }
    }
}

juce::dsp::Convolution& CabinetPedal::engineForSlot(int slot) noexcept
{
    switch (slot)
    {
        case 0:  return convA;
        case 1:  return convB;
        case 2:  return convA2;
        default: return convB2;
    }
}

bool CabinetPedal::anySlotLoaded() const noexcept
{
    for (int slot = 0; slot < numIRSlots; ++slot)
        if (slotLoaded[slot].load())
            return true;
    return false;
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

bool CabinetPedal::applyIRFile(int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numIRSlots || ! file.existsAsFile())
        return false;

    auto maxSamples = static_cast<size_t>(currentSampleRate * 2.0);

    engineForSlot(slot).loadImpulseResponse(file,
                                            juce::dsp::Convolution::Stereo::yes,
                                            juce::dsp::Convolution::Trim::yes,
                                            maxSamples,
                                            juce::dsp::Convolution::Normalise::yes);

    slotFiles[slot] = file;
    slotLoaded[slot].store(true);
    return true;
}

bool CabinetPedal::loadImpulseResponseFile(int slot, const juce::File& file)
{
    auto firstLoad = ! anySlotLoaded();

    if (! applyIRFile(slot, file))
        return false;

    // The first real IR loaded into an otherwise-synthetic cab gets the
    // same "hear it cleanly" defaults the old single loader always set:
    // the synthetic Mic/Room engines still blend in on top (Mic Blend
    // defaults to 30%, Room to 15%) and have no phase relationship
    // whatsoever to a real capture, so mixing them in produces comb
    // filtering/phasiness rather than a pleasant "two mics" blend. Blend
    // goes to whichever mic the file actually replaced (Mic A = 0, Mic B
    // = 100), Room to 0. Only on the FIRST load, so loading a second file
    // never undoes a Blend/Room the user has since dialed in on purpose.
    if (firstLoad)
    {
        micBlend.set((slot % 2 == 1) ? 100.0f : 0.0f);
        roomAmount.set(0.0f);
    }

    // A real captured IR measures much darker above ~3-4kHz than the
    // synthetic cabs' own already-tuned voicing (see Air's own comment in
    // the header) - raised here specifically, rather than in Air's overall
    // default, so the synthetic cabs' sound doesn't change for anyone not
    // using this feature. Only ever raised, never lowered: a user who has
    // already pushed Air higher keeps it.
    air.set(juce::jmax(air.get(), 45.0f));

    return true;
}

void CabinetPedal::clearImpulseResponse(int slot)
{
    if (slot < 0 || slot >= numIRSlots)
        return;

    slotLoaded[slot].store(false);
    slotFiles[slot] = juce::File();

    // The audio thread resynthesizes this slot's IR on its next block (see
    // pendingResynth) - for a Right slot that only happens while dual-cab
    // is active, and the flag simply waits until it is.
    pendingResynth[slot].store(true);
}

juce::String CabinetPedal::getImpulseResponseFileName(int slot) const
{
    if (slot < 0 || slot >= numIRSlots || ! slotLoaded[slot].load())
        return {};
    return slotFiles[slot].getFileName();
}

juce::var CabinetPedal::getExtraState() const
{
    if (! anySlotLoaded())
        return {};

    juce::Array<juce::var> paths;
    for (int slot = 0; slot < numIRSlots; ++slot)
        paths.add(slotLoaded[slot].load() ? slotFiles[slot].getFullPathName() : juce::String());

    auto* state = new juce::DynamicObject();
    state->setProperty("irFiles", paths);
    return juce::var(state);
}

void CabinetPedal::setExtraState(const juce::var& state)
{
    auto files = state.getProperty("irFiles", juce::var());
    if (! files.isArray())
        return;

    // Applies each saved path directly (not through loadImpulseResponseFile):
    // the preset already saved Blend/Room/Air as ordinary parameters, so
    // the "first load" defaults must not run over the top of them.
    for (int slot = 0; slot < numIRSlots && slot < files.size(); ++slot)
    {
        auto path = files[slot].toString();
        if (path.isEmpty())
            continue;

        juce::File file(path);
        if (! applyIRFile(slot, file))
            juce::Logger::writeToLog("Cabinet: IR file for slot " + juce::String(slot) + " is missing, using the built-in cab: " + path);
    }
}

void CabinetPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    // Anything baked into the LEFT cab's IRs: the cab itself plus its second
    // speaker and the (5%-quantized) mix. Mix only counts while there IS a
    // second speaker, so turning Spk Mix with Spk 2 = None reloads nothing.
    auto cabType = static_cast<int>(std::round(cab.get()));
    auto secondType = static_cast<int>(std::round(speaker2.get())) - 1;
    auto mixStep = secondType >= 0 ? static_cast<int>(std::round(speakerMix.get() / 5.0f)) : 0;
    auto cabSignature = (cabType * 16 + (secondType + 1)) * 32 + mixStep;
    auto cabChanged = (cabSignature != lastCab);
    lastCab = cabSignature;

    // Tell any amp in the chain which speaker it is driving (see
    // SpeakerLoadLink.h): the left cab, with a mixed second speaker blended
    // in by the same Spk Mix that blends the IRs.
    {
        auto speaker = AmpSpeakerLoad::speakerForCab(cabType);
        if (secondType >= 0)
            speaker = AmpSpeakerLoad::mixSpeakers(speaker, AmpSpeakerLoad::speakerForCab(secondType), speakerMix.get() / 100.0);
        SpeakerLoadLink::publish(speaker);
    }

    reloadMicIRIfNeeded(0, cabChanged);
    reloadMicIRIfNeeded(1, cabChanged);

    auto liveStudioType = static_cast<int>(std::round(liveStudio.get()));
    auto liveStudioChanged = (liveStudioType != lastLiveStudio);
    lastLiveStudio = liveStudioType;
    reloadRoomIRIfNeeded(liveStudioChanged);

    // Dual-cab mode: on when Cab R names a cab, or when either Right slot
    // holds a real IR (loading one there IS asking for a right-side cab).
    auto cabRIndex = static_cast<int>(std::round(cabRight.get()));
    auto dualActive = cabRIndex > 0 || slotLoaded[2].load() || slotLoaded[3].load();
    if (dualActive)
        reloadRightCabIRsIfNeeded();

    auto channelsToUse = juce::jmin(numChannels, currentNumChannels, maxChannels);

    // Speaker push: each cab's own headroom from its speaker's rated power
    // (blended by Spk Mix when a second speaker is mixed in - the same
    // blend the IR itself uses), then the dry signal through that cab's
    // processors. With Push at 0 they return their input exactly, so this
    // is a copy and nothing more. When Cab R is "Same as Cab" the right
    // side simply reuses the left cab's processed signal.
    auto headroomOf = [](int cabTypeIndex, int secondTypeIndex, float mix01)
    {
        auto headroom = SpeakerCompression::headroomForWatts(SpeakerCompression::ratedWattsForCab(cabTypeIndex));
        if (secondTypeIndex >= 0)
        {
            auto second = SpeakerCompression::headroomForWatts(SpeakerCompression::ratedWattsForCab(secondTypeIndex));
            headroom += (second - headroom) * juce::jlimit(0.0f, 1.0f, mix01);
        }
        return headroom;
    };

    auto pushAmount = speakerPush.get() / 100.0f;
    auto rightHasOwnCab = cabRIndex > 0;

    const float* leftInput[maxChannels] = {};
    const float* rightInput[maxChannels] = {};

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        auto& leftProcessor = speakerProcessors[0][ch];
        leftProcessor.setPush(pushAmount);
        leftProcessor.setHeadroom(headroomOf(cabType, secondType, speakerMix.get() / 100.0f));

        auto* leftOut = pushedLeft.getWritePointer(ch);
        for (int n = 0; n < numSamples; ++n)
            leftOut[n] = leftProcessor.processSample(channelData[ch][n]);
        leftInput[ch] = leftOut;
        rightInput[ch] = leftOut;

        if (dualActive && rightHasOwnCab)
        {
            auto& rightProcessor = speakerProcessors[1][ch];
            rightProcessor.setPush(pushAmount);
            rightProcessor.setHeadroom(headroomOf(cabRIndex - 1, static_cast<int>(std::round(speaker2R.get())) - 1,
                                                  speakerMixR.get() / 100.0f));

            auto* rightOut = pushedRight.getWritePointer(ch);
            for (int n = 0; n < numSamples; ++n)
                rightOut[n] = rightProcessor.processSample(channelData[ch][n]);
            rightInput[ch] = rightOut;
        }
    }

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        scratchA.copyFrom(ch, 0, leftInput[ch], numSamples);
        scratchB.copyFrom(ch, 0, leftInput[ch], numSamples);
        scratchRoom.copyFrom(ch, 0, leftInput[ch], numSamples);

        if (dualActive)
        {
            scratchA2.copyFrom(ch, 0, rightInput[ch], numSamples);
            scratchB2.copyFrom(ch, 0, rightInput[ch], numSamples);
        }
    }

    // Makeup gain for a slot holding a real, user-loaded IR file (not the
    // synthetic cabs, which already normalise their own peak level - see
    // generateMicIR's own applyGain call). JUCE's Convolution::Normalise::yes
    // (passed on every load) is a one-time scale of the IR itself against
    // a fixed sum-of-squared-magnitude target, not a runtime match to the
    // dry signal's own level - a real commercial cab IR, especially a
    // bright/thin close-mic capture, can still land quieter after that
    // than a synthetic cab's own tuned output. Standard commercial IR
    // loaders compensate with roughly +6 to +12dB of fixed makeup gain on
    // top of whatever their own normalisation does; +9dB (the middle of
    // that range) applied per loaded slot matches that convention without
    // touching the Level knob's own meaning for everyone else.
    constexpr float loadedIRMakeupGainDb = 9.0f;

    // Mic placement, worked out once per block: how far each mic sits, how
    // directional its type is (a ribbon has the stronger proximity
    // effect), and how much later the FARTHER mic hears the cab than the
    // nearer one - see dsp/MicPlacement.h.
    auto metresA = MicPlacement::distanceMetres(distanceA.get());
    auto metresB = MicPlacement::distanceMetres(distanceB.get());
    auto referenceMetres = MicPlacement::referenceMetres();
    auto kappaFor = [](const PedalParameter& type)
    {
        return std::round(type.get()) >= 1.0f ? MicPlacement::kappaRibbon : MicPlacement::kappaDynamic;
    };
    const float kappas[2] = { kappaFor(micTypeA), kappaFor(micTypeB) };
    const double metres[2] = { metresA, metresB };
    const float delaySamples[2] = {
        static_cast<float>(MicPlacement::relativeDelaySeconds(metresA, metresB) * currentSampleRate),
        static_cast<float>(MicPlacement::relativeDelaySeconds(metresB, metresA) * currentSampleRate)
    };

    auto runEngine = [&](juce::dsp::Convolution& conv, juce::AudioBuffer<float>& scratch, int slot)
    {
        juce::dsp::AudioBlock<float> block(scratch.getArrayOfWritePointers(), static_cast<size_t>(channelsToUse), static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> context(block);
        conv.process(context);

        if (slot < 0)
            return; // the room engine has no mic placement

        auto loaded = slotLoaded[slot].load();
        if (loaded)
            block.multiplyBy(juce::Decibels::decibelsToGain(loadedIRMakeupGainDb));

        // Slots 0/2 carry Mic A, 1/3 Mic B. A loaded real IR skips the
        // proximity shelf (its capture already has one) but keeps the delay.
        auto mic = slot % 2;
        auto& state = placement[slot];
        for (int ch = 0; ch < channelsToUse; ++ch)
        {
            state.delay[ch].setDelaySamples(delaySamples[mic]);
            if (! loaded)
                state.shelf[ch].setPlacement(kappas[mic], metres[mic], referenceMetres);

            auto* data = scratch.getWritePointer(ch);
            for (int n = 0; n < numSamples; ++n)
            {
                auto sample = loaded ? data[n] : state.shelf[ch].processSample(data[n]);
                data[n] = state.delay[ch].processSample(sample);
            }
        }
    };

    runEngine(convA, scratchA, 0);
    runEngine(convB, scratchB, 1);
    runEngine(convRoom, scratchRoom, -1);
    if (dualActive)
    {
        runEngine(convA2, scratchA2, 2);
        runEngine(convB2, scratchB2, 3);
    }

    auto blend = micBlend.get() / 100.0f;     // 0 = all Mic A, 1 = all Mic B
    auto roomMix = roomAmount.get() / 100.0f; // 0 = no room mic, 1 = all room mic
    auto invertA = polarityA.get() >= 0.5f;
    auto invertB = polarityB.get() >= 0.5f;

    // A mono chain has only one output to put both cabs in, so it always
    // gets the even sum (same as Spread = 0), whatever Spread says.
    auto weights = channelsToUse >= 2 ? DualCabMix::weightsForSpread(spread.get() / 100.0f)
                                       : DualCabMix::Weights { 0.5f, 0.5f };

    // Mic Spread needs two sides to pan across; a mono chain ignores it.
    auto micSpreadAmount = channelsToUse >= 2 ? micSpread.get() / 100.0f : 0.0f;

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        auto* out = channelData[ch];
        auto* a = scratchA.getReadPointer(ch);
        auto* b = scratchB.getReadPointer(ch);
        auto* a2 = scratchA2.getReadPointer(ch);
        auto* b2 = scratchB2.getReadPointer(ch);
        auto* room = scratchRoom.getReadPointer(ch);

        for (int n = 0; n < numSamples; ++n)
        {
            auto micSignal = DualCabMix::mixMicsWithSpread(a[n], b[n], blend, invertA, invertB, micSpreadAmount, ch);

            if (dualActive)
            {
                // Each side's own cab is its "primary" (Cab on the left,
                // Cab R on the right); the other side's cab is the
                // secondary, weighted in by how far Spread is pulled in.
                auto rightCab = DualCabMix::mixMicsWithSpread(a2[n], b2[n], blend, invertA, invertB, micSpreadAmount, ch);
                auto primary = (ch == 0) ? micSignal : rightCab;
                auto secondary = (ch == 0) ? rightCab : micSignal;
                micSignal = weights.primary * primary + weights.secondary * secondary;
            }

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
             &polarityA, &polarityB, &micSpread, &distanceA, &distanceB, &cabRight, &spread,
             &speaker2, &speakerMix, &speaker2R, &speakerMixR, &speakerPush,
             &liveStudio, &roomAmount, &lowCut, &highCut, &resonance, &air, &width, &level };
}

std::unique_ptr<juce::Component> CabinetPedal::createCustomEditor()
{
    return std::make_unique<CabinetVisualEditor>(*this, CabinetVisualEditor::Bindings {
        cab, micTypeA, micPositionA, micTypeB, micPositionB, micBlend,
        distanceA, distanceB, cabRight, speaker2, speakerMix, speaker2R, speakerMixR, speakerPush });
}

// What the editor CONTROLS, so the Inspector doesn't also show each as a knob.
// (Mic Blend and Spk Push are only read by it - for the dimming and the heat
// glow - and stay as knobs.)
std::vector<PedalParameter*> CabinetPedal::getCustomEditorHandledParameters()
{
    return { &cab, &micTypeA, &micPositionA, &micTypeB, &micPositionB,
             &distanceA, &distanceB, &cabRight, &speaker2, &speakerMix, &speaker2R, &speakerMixR };
}
