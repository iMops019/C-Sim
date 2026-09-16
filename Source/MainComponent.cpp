#include "MainComponent.h"
#include "ReverbPedal.h"
#include "NoiseGatePedal.h"
#include "TransientShaperPedal.h"
#include "CleanAmpPedal.h"
#include "Peavey5150Pedal.h"
#include "FourByTwelveCabPedal.h"
#include "TriodeStagePedal.h"
#include "ToneStackPedal.h"
#include "PowerAmpPedal.h"
#include "DiodeClipperPedal.h"

namespace
{
    std::vector<PedalListComponent::CatalogItem> makePedalCatalog()
    {
        return { { "Reverb", [] { return std::make_unique<ReverbPedal>(); } },
                 { "Noise Gate", [] { return std::make_unique<NoiseGatePedal>(); } },
                 { "Transient Shaper", [] { return std::make_unique<TransientShaperPedal>(); } } };
    }

    std::vector<PedalListComponent::CatalogItem> makeAmpCatalog()
    {
        return { { "Drop Tuned Clean 1", [] { return std::make_unique<CleanAmpPedal>(); } },
                 { "5150 Lead", [] { return std::make_unique<Peavey5150Pedal>(); } } };
    }

    std::vector<PedalListComponent::CatalogItem> makeCabCatalog()
    {
        return { { "4x12 V30", [] { return std::make_unique<FourByTwelveCabPedal>(); } } };
    }

    // Raw circuit-level building blocks - stack these yourself (Triode
    // Stage, Tone Stack, Diode Clipper, Power Amp, in whatever order and
    // however many you like) to build your own amp from scratch, rather
    // than only using the fixed preset amps above.
    std::vector<PedalListComponent::CatalogItem> makeLabCatalog()
    {
        return { { "Triode Stage", [] { return std::make_unique<TriodeStagePedal>(); } },
                 { "Tone Stack", [] { return std::make_unique<ToneStackPedal>(); } },
                 { "Diode Clipper", [] { return std::make_unique<DiodeClipperPedal>(); } },
                 { "Power Amp", [] { return std::make_unique<PowerAmpPedal>(); } } };
    }
}

MainComponent::MainComponent()
    : pedalList(signalChain, makePedalCatalog()),
      ampList(signalChain, makeAmpCatalog()),
      cabList(signalChain, makeCabCatalog()),
      labList(signalChain, makeLabCatalog())
{
    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [this] { openSettings(); };

    addAndMakeVisible(tunerButton);
    tunerButton.onClick = [this] { openTuner(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::topLeft);

    masterVolumeLabel.setText("Master Volume", juce::dontSendNotification);
    masterVolumeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolumeLabel);

    masterVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    masterVolumeSlider.setTextValueSuffix("%");
    masterVolumeSlider.setRange(0.0, 150.0, 1.0);
    masterVolumeSlider.setValue(100.0, juce::dontSendNotification);
    masterVolumeSlider.onValueChange = [this]
    {
        masterVolumeGain.store((float) masterVolumeSlider.getValue() / 100.0f, std::memory_order_relaxed);
    };
    addAndMakeVisible(masterVolumeSlider);

    addAndMakeVisible(signalChain);

    tabs.addTab("Pedals", juce::Colours::darkgrey, &pedalList, false);
    tabs.addTab("Amps", juce::Colours::darkgrey, &ampList, false);
    tabs.addTab("Cabs", juce::Colours::darkgrey, &cabList, false);
    tabs.addTab("Lab", juce::Colours::darkgrey, &labList, false);
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
    tunerWindow = nullptr;
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    signalChain.setAudioConfig(sampleRate, samplesPerBlockExpected, 2);
    tunerEngine.setSampleRate(sampleRate);
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

    // Feed the tuner from the dry, pre-effects signal so pedals don't throw
    // off pitch detection.
    tunerEngine.pushSamples(buffer->getReadPointer(0, bufferToFill.startSample), bufferToFill.numSamples);

    auto numChannelsForFx = juce::jmin(numOutBuses, 2);
    if (numChannelsForFx > 0)
    {
        float* channelPtrs[2] = { nullptr, nullptr };
        for (int channel = 0; channel < numChannelsForFx; ++channel)
            channelPtrs[channel] = buffer->getWritePointer(channel, bufferToFill.startSample);

        signalChain.processBlock(channelPtrs, numChannelsForFx, bufferToFill.numSamples);

        auto gain = masterVolumeGain.load(std::memory_order_relaxed);
        for (int channel = 0; channel < numChannelsForFx; ++channel)
            juce::FloatVectorOperations::multiply(channelPtrs[channel], gain, bufferToFill.numSamples);
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
    settingsButton.setBounds(topBar.removeFromRight(100).removeFromTop(28));
    topBar.removeFromRight(8);
    tunerButton.setBounds(topBar.removeFromRight(80).removeFromTop(28));
    topBar.removeFromRight(10);

    auto volumeArea = topBar.removeFromRight(220);
    masterVolumeLabel.setBounds(volumeArea.removeFromTop(16));
    masterVolumeSlider.setBounds(volumeArea.removeFromTop(24));

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

void MainComponent::openTuner()
{
    if (tunerWindow != nullptr)
    {
        tunerWindow->toFront(true);
        return;
    }

    tunerWindow = std::make_unique<TunerWindow>(tunerEngine, [this] { tunerWindow = nullptr; });
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
