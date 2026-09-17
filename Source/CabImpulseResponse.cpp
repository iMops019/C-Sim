#include "CabImpulseResponse.h"

#include <cmath>

namespace CabImpulseResponse
{

namespace
{
    struct ResonantMode
    {
        float frequencyHz;
        float amplitude;
        float decaySeconds;
    };

    // Tight, modern, percussive - the classic 5150/6505 half-stack pairing.
    // Shorter decay times throughout (less smear/boom), low body mode
    // pulled back so it doesn't dominate, upper-mid presence pushed up so
    // pick attack cuts through instead of getting buried.
    constexpr ResonantMode v30Modes[] = {
        { 100.0f,  0.55f, 0.030f },
        { 180.0f,  0.35f, 0.025f },
        { 400.0f,  0.25f, 0.020f },
        { 800.0f,  0.22f, 0.016f },
        { 1200.0f, 0.20f, 0.014f },
        { 2500.0f, 0.55f, 0.012f },
        { 3200.0f, 0.50f, 0.010f },
        { 4000.0f, 0.35f, 0.008f },
        { 4800.0f, 0.22f, 0.006f },
    };

    // Vintage, looser, warmer - longer decays (boomier), a midrange hump
    // around 600Hz (the classic Greenback character) instead of the V30's
    // scooped upper-mid, and a smoother, earlier top-end rolloff.
    constexpr ResonantMode greenbackModes[] = {
        { 90.0f,   0.50f, 0.045f },
        { 160.0f,  0.40f, 0.040f },
        { 350.0f,  0.30f, 0.035f },
        { 600.0f,  0.45f, 0.030f },
        { 850.0f,  0.35f, 0.025f },
        { 1800.0f, 0.30f, 0.018f },
        { 2800.0f, 0.28f, 0.014f },
        { 3500.0f, 0.15f, 0.010f },
    };

    // Airier, less low-end reinforcement (open-backed cabs don't couple
    // bass the way a sealed 4x12 does), quicker decay, top end extends
    // further out before rolling off.
    constexpr ResonantMode openBackModes[] = {
        { 110.0f,  0.30f, 0.018f },
        { 220.0f,  0.22f, 0.016f },
        { 500.0f,  0.20f, 0.014f },
        { 900.0f,  0.18f, 0.012f },
        { 1500.0f, 0.25f, 0.010f },
        { 2800.0f, 0.40f, 0.010f },
        { 3800.0f, 0.38f, 0.009f },
        { 5200.0f, 0.30f, 0.007f },
    };

    // Small and boxy - a pronounced low-mid cabinet-resonance bump (the
    // "combo box" honk) and an earlier top-end rolloff than any of the
    // 4x12/2x12 cabs.
    constexpr ResonantMode comboModes[] = {
        { 120.0f,  0.40f, 0.022f },
        { 260.0f,  0.50f, 0.028f },
        { 450.0f,  0.35f, 0.020f },
        { 700.0f,  0.25f, 0.016f },
        { 1400.0f, 0.22f, 0.012f },
        { 2200.0f, 0.30f, 0.010f },
        { 3000.0f, 0.20f, 0.008f },
    };

    struct ModeSet
    {
        const ResonantMode* modes;
        size_t count;
        double lowCutHz;
    };

    ModeSet modeSetForCab(int cabType)
    {
        switch (cabType)
        {
            case cabFourByTwelveGreenback: return { greenbackModes, sizeof(greenbackModes) / sizeof(greenbackModes[0]), 80.0 };
            case cabTwoByTwelveOpenBack:   return { openBackModes, sizeof(openBackModes) / sizeof(openBackModes[0]), 100.0 };
            case cabOneByTwelveCombo:      return { comboModes, sizeof(comboModes) / sizeof(comboModes[0]), 110.0 };
            case cabFourByTwelveV30:
            default:                        return { v30Modes, sizeof(v30Modes) / sizeof(v30Modes[0]), 90.0 };
        }
    }

    // A mic's own frequency-response character, layered on top of
    // whichever cab it's pointed at - not a full separate mic model, just
    // enough shaping to make Dynamic vs Ribbon a clear, recognizable
    // difference rather than a label with no sonic effect.
    void applyMicColoration(float* data, int numSamples, double sampleRate, int micType)
    {
        if (micType == micRibbon)
        {
            juce::IIRFilter highRolloff, lowMidBump;
            highRolloff.setCoefficients(juce::IIRCoefficients::makeHighShelf(sampleRate, 5000.0, 0.7, 0.5f));
            lowMidBump.setCoefficients(juce::IIRCoefficients::makePeakFilter(sampleRate, 300.0, 1.0, 1.3f));
            highRolloff.processSamples(data, numSamples);
            lowMidBump.processSamples(data, numSamples);
        }
        else // micDynamic
        {
            juce::IIRFilter presencePeak;
            presencePeak.setCoefficients(juce::IIRCoefficients::makePeakFilter(sampleRate, 4500.0, 1.2, 1.6f));
            presencePeak.processSamples(data, numSamples);
        }
    }

    // Mic position, continuously: a capsule aimed at the cone's edge picks
    // up more of the higher-frequency cone-breakup modes (bright, thin);
    // aimed dead-center picks up more of the lower fundamental cone-motion
    // modes (dark, boomy). Each mode's own weight slides between those two
    // extremes so Position is a real, continuous tonal control rather than
    // a hard A/B switch.
    void renderMicChannel(float* data, int numSamples, double sampleRate,
                           const ResonantMode* modes, size_t numModes, float positionFraction,
                           float phaseOffsetRadians, int sampleDelay)
    {
        for (int n = sampleDelay; n < numSamples; ++n)
        {
            auto t = static_cast<float>(n - sampleDelay) / static_cast<float>(sampleRate);
            float value = 0.0f;

            for (size_t m = 0; m < numModes; ++m)
            {
                auto& mode = modes[m];
                auto edgeWeight = juce::jlimit(0.0f, 1.0f, (mode.frequencyHz - 300.0f) / 2000.0f);

                // How well this mode's character matches the requested
                // position: 1 = fully favored (this mode gets full
                // amplitude), 0 = fully disfavored. Floored at minWeight
                // rather than 0 - Position should shift tonal emphasis,
                // not act as a second, hidden volume knob. A prior version
                // let every mode's weight sag to as little as ~0.3 at the
                // default Position settings (attenuating even the cab's
                // characteristic bite resonances) while the attack
                // transient below - which the peak-normalisation is keyed
                // off - stayed at full amplitude, making the whole cab
                // sound quiet and dull.
                constexpr float minWeight = 0.4f;
                auto favour = 1.0f - std::abs(edgeWeight - (1.0f - positionFraction));
                auto weight = minWeight + (1.0f - minWeight) * favour;

                auto phase = juce::MathConstants<float>::twoPi * mode.frequencyHz * t + phaseOffsetRadians;
                value += mode.amplitude * weight * std::sin(phase) * std::exp(-t / mode.decaySeconds);
            }

            data[n] = value;
        }

        // Sharp initial transient (cone/speaker attack) on top of the modes.
        auto attackSamples = juce::jmin(numSamples - sampleDelay, static_cast<int>(0.001 * sampleRate));
        for (int n = 0; n < attackSamples; ++n)
        {
            auto idx = sampleDelay + n;
            auto envelope = 1.0f - (static_cast<float>(n) / static_cast<float>(attackSamples));
            data[idx] += 1.3f * envelope * envelope;
        }

        // Fade the tail out so truncating the IR doesn't click.
        auto fadeSamples = juce::jmin(numSamples / 10, static_cast<int>(0.01 * sampleRate));
        for (int i = 0; i < fadeSamples; ++i)
        {
            auto idx = numSamples - fadeSamples + i;
            if (idx < 0 || idx >= numSamples)
                continue;

            auto gain = 1.0f - (static_cast<float>(i) / static_cast<float>(fadeSamples));
            data[idx] *= gain;
        }
    }

    struct RoomReflection
    {
        double timeMs;
        float amplitude;
    };

    // Tighter, boxier, short small-stage/rehearsal-room slap.
    constexpr RoomReflection liveReflections[] = {
        { 4.0, 0.9f }, { 9.0, 0.55f }, { 15.0, 0.4f }, { 24.0, 0.28f }, { 38.0, 0.18f }, { 60.0, 0.10f }
    };

    // Longer, smoother, more diffuse treated-room decay.
    constexpr RoomReflection studioReflections[] = {
        { 6.0, 0.8f }, { 14.0, 0.5f }, { 26.0, 0.4f }, { 45.0, 0.3f }, { 75.0, 0.22f },
        { 120.0, 0.15f }, { 180.0, 0.09f }, { 260.0, 0.05f }
    };

    void renderRoomChannel(float* data, int numSamples, double sampleRate,
                            const RoomReflection* reflections, size_t numReflections,
                            int randomSeed, int sampleDelay)
    {
        std::fill(data, data + numSamples, 0.0f);

        // A soft "direct" click standing in for the cab's own on-axis
        // transient - quieter than the close-mic IR's, since a room mic
        // is further away and picking up mostly the room, not the attack.
        auto directSamples = juce::jmin(numSamples, static_cast<int>(0.0015 * sampleRate));
        for (int n = 0; n < directSamples; ++n)
        {
            auto envelope = 1.0f - (static_cast<float>(n) / static_cast<float>(directSamples));
            data[n] += 0.35f * envelope * envelope;
        }

        // Each reflection is a short burst of lightly lowpassed white
        // noise, not a single fixed-pitch tone - real room reflections
        // are broadband/noise-like, not tonal. A previous version used a
        // fixed 700Hz sine carrier for every single reflection, which
        // convolved into the guitar signal as a narrow, artificial
        // resonance ringing at exactly 700Hz on every reflection instead
        // of genuine diffuse room "air" - audible as an unnatural boxy/
        // muffled coloration that got worse the more Room was used,
        // rather than sounding like more of an actual room. A one-pole
        // lowpass smooths the raw noise into something closer to a real
        // reflection's own high-frequency air absorption, rather than
        // leaving it as harsh full-spectrum hiss.
        juce::Random rng(randomSeed);
        for (size_t r = 0; r < numReflections; ++r)
        {
            auto delaySamples = sampleDelay + static_cast<int>(reflections[r].timeMs / 1000.0 * sampleRate);
            if (delaySamples < 0 || delaySamples >= numSamples)
                continue;

            // A short decaying burst rather than a single delta per
            // reflection, so it reads as diffuse room sound rather than
            // discrete slap-back echoes.
            auto burstSamples = juce::jmin(numSamples - delaySamples, static_cast<int>(0.004 * sampleRate));
            float lpState = 0.0f;
            constexpr float lpCoeff = 0.35f; // smooths raw noise into a softer, less hissy burst
            for (int n = 0; n < burstSamples; ++n)
            {
                auto t = static_cast<float>(n) / static_cast<float>(sampleRate);
                auto whiteNoise = rng.nextFloat() * 2.0f - 1.0f;
                lpState += lpCoeff * (whiteNoise - lpState);
                data[delaySamples + n] += reflections[r].amplitude * lpState * std::exp(-t / 0.006f);
            }
        }

        auto fadeSamples = juce::jmin(numSamples / 8, static_cast<int>(0.02 * sampleRate));
        for (int i = 0; i < fadeSamples; ++i)
        {
            auto idx = numSamples - fadeSamples + i;
            if (idx < 0 || idx >= numSamples)
                continue;

            auto gain = 1.0f - (static_cast<float>(i) / static_cast<float>(fadeSamples));
            data[idx] *= gain;
        }
    }
}

juce::AudioBuffer<float> generateMicIR(double sampleRate, int cabType, int micType, float positionFraction)
{
    // Short - a real cab IR is short; this just needs to carry the cab's
    // own resonant character, not a long reverberant tail.
    constexpr double durationSeconds = 0.08;
    auto numSamples = juce::jmax(64, static_cast<int>(sampleRate * durationSeconds));

    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();

    auto modeSet = modeSetForCab(cabType);
    auto position = juce::jlimit(0.0f, 1.0f, positionFraction);

    // Two near-identical channels with a small delay/phase offset between
    // them, approximating this one mic's own capsule width for some
    // stereo image (separate from the two-mic Blend, which is a whole
    // second IR generated with its own settings).
    renderMicChannel(buffer.getWritePointer(0), numSamples, sampleRate, modeSet.modes, modeSet.count, position, 0.0f, 0);
    renderMicChannel(buffer.getWritePointer(1), numSamples, sampleRate, modeSet.modes, modeSet.count, position, 0.35f,
                      static_cast<int>(0.0003 * sampleRate));

    // Only a low cut here - no baked-in high cut. CabinetPedal has its own
    // adjustable High Cut trim applied after mixing; baking a second,
    // fixed high-cut into the IR itself (this used to sit at 5.5-8kHz per
    // cab) stacked with that trim, compounding into a noticeably darker/
    // more muffled top end than either filter alone, with no way for the
    // High Cut knob to ever brighten past what the IR had already thrown
    // away. The resonant modes above already roll off naturally past
    // ~5kHz on their own (see the mode tables) without needing an extra
    // filter on top.
    juce::IIRFilter lowCut;
    lowCut.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, modeSet.lowCutHz));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        lowCut.processSamples(data, numSamples);
        lowCut.reset();

        applyMicColoration(data, numSamples, sampleRate, micType);
    }

    buffer.applyGain(0.9f / juce::jmax(0.0001f, buffer.getMagnitude(0, numSamples)));

    return buffer;
}

juce::AudioBuffer<float> generateRoomIR(double sampleRate, bool isStudio)
{
    auto durationSeconds = isStudio ? 0.32 : 0.14;
    auto numSamples = juce::jmax(64, static_cast<int>(sampleRate * durationSeconds));

    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();

    if (isStudio)
    {
        renderRoomChannel(buffer.getWritePointer(0), numSamples, sampleRate, studioReflections,
                           sizeof(studioReflections) / sizeof(studioReflections[0]), 1001, 0);
        renderRoomChannel(buffer.getWritePointer(1), numSamples, sampleRate, studioReflections,
                           sizeof(studioReflections) / sizeof(studioReflections[0]), 2002,
                           static_cast<int>(0.0005 * sampleRate));
    }
    else
    {
        renderRoomChannel(buffer.getWritePointer(0), numSamples, sampleRate, liveReflections,
                           sizeof(liveReflections) / sizeof(liveReflections[0]), 1001, 0);
        renderRoomChannel(buffer.getWritePointer(1), numSamples, sampleRate, liveReflections,
                           sizeof(liveReflections) / sizeof(liveReflections[0]), 2002,
                           static_cast<int>(0.0005 * sampleRate));
    }

    juce::IIRFilter lowCut, highCut;
    lowCut.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 120.0));
    highCut.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, isStudio ? 4500.0 : 3500.0, 0.7f));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        lowCut.processSamples(data, numSamples);
        highCut.processSamples(data, numSamples);
        lowCut.reset();
        highCut.reset();

        if (! isStudio)
        {
            // A bit of small-room "box" honk that a treated studio room
            // wouldn't have.
            juce::IIRFilter boxBump;
            boxBump.setCoefficients(juce::IIRCoefficients::makePeakFilter(sampleRate, 500.0, 1.2, 1.3f));
            boxBump.processSamples(data, numSamples);
        }
    }

    buffer.applyGain(0.9f / juce::jmax(0.0001f, buffer.getMagnitude(0, numSamples)));

    return buffer;
}

}
