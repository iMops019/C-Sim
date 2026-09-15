#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [this] { openSettings(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);

    setSize(600, 400);

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

void MainComponent::prepareToPlay(int /*samplesPerBlockExpected*/, double /*sampleRate*/)
{
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Straight passthrough: copy each input channel to the matching output
    // channel with no processing. No amp/pedal stages yet.
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

    if (numInputChannels == 1 && numOutputChannels >= 2 && numOutBuses >= 2)
    {
        // Mono guitar input -> duplicated to both output channels, so a
        // single-input interface still plays centred on both speakers/ears.
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
    auto area = getLocalBounds().reduced(20);
    settingsButton.setBounds(area.removeFromTop(30).removeFromRight(120));
    area.removeFromTop(20);
    statusLabel.setBounds(area);
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
    text << "Sample rate: " << device->getCurrentSampleRate() << " Hz\n";
    text << "Buffer size: " << device->getCurrentBufferSizeSamples() << " samples\n";
    text << "Passthrough active - no amp/pedal processing yet.";

    statusLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    updateStatusLabel();
}
