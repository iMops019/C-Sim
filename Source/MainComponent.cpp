#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [this] { openSettings(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::topLeft);

    addAndMakeVisible(signalChain);

    tabs.addTab("Pedals", juce::Colours::darkgrey, &pedalList, false);
    addAndMakeVisible(tabs);

    setSize(800, 600);

    // Open with 0 input channels so nothing is captured/played until the
    // user picks their interface in Settings; output stays on the system
    // default so the device selector has something initialised to show.
    setAudioChannels(0, 2);

    deviceManager.addChangeListener(this);
    updateStatusLabel();
}

MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener(this);
    settingsWindow = nullptr;
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    signalChain.setAudioConfig(sampleRate, samplesPerBlockExpected, 2);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Straight passthrough: copy each input channel to the matching output
    // channel with no processing, then run the result through whatever
    // pedals the user has manually added to the Signal Chain.
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    auto activeInputChannels = device->getActiveInputChannels();
    auto activeOutputChannels = device->getActiveOutputChannels();
    auto numInputChannels = activeInputChannels.countNumberOfSetBits();
    auto numOutputChannels = activeOutputChannels.countNumberOfSetBits();

    auto* buffer = bufferToFill.buffer;
    auto numOutBuses = buffer->getNumChannels();

    if (numInputChannels >= 1 && numOutputChannels >= 2 && numOutBuses >= 2)
    {
        // Guitar is plugged into a single input jack (channel 1), but the
        // device selector only lets you enable input channels in pairs, so
        // channel 2 usually reads as "active" while carrying silence. Always
        // duplicate channel 1 to both outputs so you hear it in both ears
        // regardless of how many input channels the device reports active.
        buffer->copyFrom(1, bufferToFill.startSample, *buffer, 0, bufferToFill.startSample, bufferToFill.numSamples);

        for (int channel = 2; channel < numOutBuses; ++channel)
            buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
    }
    else
    {
        auto numChannelsToCopy = juce::jmin(numInputChannels, numOutputChannels, numOutBuses);

        for (int channel = 0; channel < numChannelsToCopy; ++channel)
            buffer->copyFrom(channel, bufferToFill.startSample, *buffer, channel, bufferToFill.startSample, bufferToFill.numSamples);

        for (int channel = numChannelsToCopy; channel < numOutBuses; ++channel)
            buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
    }

    auto numChannelsForFx = juce::jmin(numOutBuses, 2);
    if (numChannelsForFx > 0)
    {
        float* channelPtrs[2] = { nullptr, nullptr };
        for (int channel = 0; channel < numChannelsForFx; ++channel)
            channelPtrs[channel] = buffer->getWritePointer(channel, bufferToFill.startSample);

        signalChain.processBlock(channelPtrs, numChannelsForFx, bufferToFill.numSamples);
    }
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto topBar = area.removeFromTop(45);
    settingsButton.setBounds(topBar.removeFromRight(120).removeFromTop(28));
    statusLabel.setBounds(topBar);

    area.removeFromTop(10);

    auto tabsArea = area.removeFromBottom(200);
    tabs.setBounds(tabsArea);

    area.removeFromBottom(10);
    signalChain.setBounds(area);
}

void MainComponent::openSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront(true);
        return;
    }

    settingsWindow = std::make_unique<SettingsWindow>(deviceManager, [this] { settingsWindow = nullptr; });
}

void MainComponent::updateStatusLabel()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        statusLabel.setText("No audio device selected yet.\nOpen Settings to choose your interface.",
                             juce::dontSendNotification);
        return;
    }

    juce::String text;
    text << "Device: " << device->getName() << "\n";
    text << "Sample rate: " << device->getCurrentSampleRate() << " Hz   ";
    text << "Buffer: " << device->getCurrentBufferSizeSamples() << " samples";

    statusLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    updateStatusLabel();
}
