#include "FenderStyleAmpPedal.h"

#include <algorithm>
#include <cmath>

FenderStyleAmpPedal::FenderStyleAmpPedal() = default;

void FenderStyleAmpPedal::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;

    driveClippers.clear();
    driveOversamplers.clear();
    amps.clear();
    toneStacks.clear();
    hiTrebleFilters.clear();
    powerAmps.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        driveClippers.emplace_back();
        driveOversamplers.emplace_back(sampleRate);
        amps.push_back(std::make_unique<FenderStyleAmp>(sampleRate));
        toneStacks.push_back(std::make_unique<BassmanToneStack>(sampleRate));
        hiTrebleFilters.emplace_back();
        powerAmps.push_back(std::make_unique<PowerAmpStage>(sampleRate));
    }

    scratch.resize(static_cast<size_t>(maximumBlockSize));
}

void FenderStyleAmpPedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, maxChannels, static_cast<int>(amps.size()));

    if (scratch.size() < static_cast<size_t>(numSamples))
        scratch.resize(static_cast<size_t>(numSamples));

    // Boost-only high-shelf at the real, sourced R42/C42 corner (~3.4kHz)
    // - fully in when the Hi-Treble switch is "On", unity (0dB) otherwise.
    // See the header comment for what this approximates and why.
    auto hiTrebleOn = std::round(hiTreble.get()) >= 0.5f;
    auto hiTrebleCoeffs = juce::IIRCoefficients::makeHighShelf(
        currentSampleRate, 3400.0, 0.707f, hiTrebleOn ? juce::Decibels::decibelsToGain(10.0f) : 1.0f);

    auto outputGain = volume.get() / 100.0f;

    // Twin Reverb's real, sourced regime (near-zero sag, heavy feedback)
    // vs. a looser, more tweed-like one - see the header comment.
    auto tightOn = std::round(tight.get()) >= 0.5f;
    auto sagAmount = tightOn ? tightSagAmount : looseSagAmount;
    auto feedbackAmount = tightOn ? tightFeedbackAmount : looseFeedbackAmount;

    auto tubeIndex = juce::jlimit(0, 3, static_cast<int>(std::round(tube.get())));
    auto tubeMu = tubeMuValues[static_cast<size_t>(tubeIndex)];

    auto driveOn = std::round(drive.get()) >= 0.5f;
    auto driveToneHz = juce::jmap(driveTone.get() / 100.0f, 800.0f, 9000.0f);
    auto driveToneCoeffs = juce::IIRCoefficients::makeLowPass(currentSampleRate, driveToneHz);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        // Lightweight built-in Drive - a small boost stage in front of
        // the amp, bypassed cleanly when Off (see header comment).
        if (driveOn)
        {
            driveOversamplers[static_cast<size_t>(ch)].processBlock(
                channelData[ch], channelData[ch], numSamples,
                [this, ch](float x) { return driveClippers[static_cast<size_t>(ch)].processSample(x * builtInDriveGain); });
        }
        driveToneFilters[ch].setCoefficients(driveToneCoeffs);
        driveToneFilters[ch].processSamples(channelData[ch], numSamples);

        // Volume moved out of FenderStyleAmp - it now applies once at the
        // very end of this chain instead, after the tone stack/Hi-Treble,
        // matching the real amp's signal order (and the established
        // pattern in FenderTwinReverbPreampPedal etc: preamp -> tone
        // stack -> recovery gain -> ... -> Volume last).
        amps[static_cast<size_t>(ch)]->setGain(gain.get() / 100.0f);
        amps[static_cast<size_t>(ch)]->setVolume(1.0f);
        amps[static_cast<size_t>(ch)]->setMu(tubeMu);
        amps[static_cast<size_t>(ch)]->processBlock(channelData[ch], scratch.data(), numSamples);

        auto& toneStack = *toneStacks[static_cast<size_t>(ch)];
        toneStack.setControls(treble.get() / 100.0f, bass.get() / 100.0f, mid.get() / 100.0f);
        for (int n = 0; n < numSamples; ++n)
            scratch[static_cast<size_t>(n)] = toneStack.processSample(scratch[static_cast<size_t>(n)]) * toneStackRecoveryGain;

        hiTrebleFilters[static_cast<size_t>(ch)].setCoefficients(hiTrebleCoeffs);
        hiTrebleFilters[static_cast<size_t>(ch)].processSamples(scratch.data(), numSamples);

        juce::FloatVectorOperations::multiply(scratch.data(), outputGain, numSamples);

        auto& powerAmp = *powerAmps[static_cast<size_t>(ch)];
        powerAmp.setSag(sagAmount);
        powerAmp.setFeedback(feedbackAmount);
        powerAmp.setDrive(0.0f); // already properly gain-staged upstream, see header comment
        powerAmp.processBlock(scratch.data(), scratch.data(), numSamples);

        // Safety ceiling: several cascaded gain stages (preamp, tone
        // stack recovery gain, Hi-Treble boost, the power amp's own
        // positive-feedback loop) can compound into peaks well above 1.0
        // for some knob combinations - a real bug found this way (a
        // realistic hard-picked power chord measured ~14-22x input peak
        // before this was added), which real audio hardware hard-clips
        // into harsh, "robotic"-sounding digital distortion. tanh is
        // transparent for normal playing levels (tanh(0.3)=~0.29) and
        // only gently catches excessive peaks, same idiom already used
        // for this toolkit's soft clippers (Morning Glory, Precision
        // Drive) - not a tone-shaping choice, purely a last-resort net.
        for (int n = 0; n < numSamples; ++n)
            scratch[static_cast<size_t>(n)] = std::tanh(scratch[static_cast<size_t>(n)]);

        std::copy(scratch.begin(), scratch.begin() + numSamples, channelData[ch]);
    }
}

std::vector<PedalParameter*> FenderStyleAmpPedal::getParameters()
{
    return { &drive, &driveTone, &gain, &tube, &treble, &bass, &mid, &volume, &hiTreble, &tight };
}
