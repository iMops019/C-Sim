#include "PrecisionDrivePedal.h"

PrecisionDrivePedal::PrecisionDrivePedal() = default;

void PrecisionDrivePedal::prepare(double sampleRate, int /*maximumBlockSize*/, int numChannels)
{
    stages.clear();
    gates.clear();
    for (int ch = 0; ch < juce::jmax(1, numChannels); ++ch)
    {
        stages.emplace_back(sampleRate);
        gates.emplace_back(sampleRate);
    }
}

void PrecisionDrivePedal::process(float* const* channelData, int numChannels, int numSamples)
{
    auto numChannelsToProcess = juce::jmin(numChannels, static_cast<int>(stages.size()));

    // Gate threshold follows the single Gate knob, same as the real
    // pedal's one-knob gate: left is permissive (barely gates), right is
    // aggressive (cuts sustain/noise hard, per Horizon's own guide noting
    // it can interfere with soloing at high settings).
    auto gateThresholdDb = juce::jmap(gate.get() / 100.0f, 0.0f, 1.0f, -70.0f, -20.0f);

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        auto& stage = stages[static_cast<size_t>(ch)];
        stage.setAttack(attack.get() / 100.0f);
        stage.setDrive(drive.get() / 100.0f);
        stage.setBright(bright.get() / 100.0f);
        stage.processBlock(channelData[ch], channelData[ch], numSamples);

        auto& noiseGate = gates[static_cast<size_t>(ch)];
        noiseGate.setThresholdDb(gateThresholdDb);
        noiseGate.setReleaseMs(120.0f);
        noiseGate.processBlock(channelData[ch], channelData[ch], numSamples);
    }

    auto outGain = volume.get() / 100.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply(channelData[ch], outGain, numSamples);
}

std::vector<PedalParameter*> PrecisionDrivePedal::getParameters()
{
    return { &attack, &drive, &bright, &gate, &volume };
}
