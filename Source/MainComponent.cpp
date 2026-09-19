#include "MainComponent.h"
#include "PedalCatalog.h"
#include "PresetManager.h"
#include "ModernLookAndFeel.h"

MainComponent::MainComponent()
    : pedalList(signalChain, PedalCatalog::pedals()),
      ampList(signalChain, PedalCatalog::amps()),
      cabList(signalChain, PedalCatalog::cabs()),
      labList(signalChain, PedalCatalog::lab())
{
    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [this] { openSettings(); };

    addAndMakeVisible(tunerButton);
    tunerButton.onClick = [this] { openTuner(); };

    addAndMakeVisible(newPresetButton);
    newPresetButton.onClick = [this] { startNewPreset(); };
    newPresetButton.setTooltip("Clear the rack and start again from a blank slate. Save the current "
                                "chain first if you want to keep it.");

    addAndMakeVisible(savePresetButton);
    savePresetButton.onClick = [this] { openSavePresetDialog(); };
    savePresetButton.setTooltip("Save the current chain as a named preset you can reload later.");

    addAndMakeVisible(loadPresetButton);
    loadPresetButton.onClick = [this] { openLoadPresetMenu(); };

    addAndMakeVisible(saveSettingsButton);
    saveSettingsButton.onClick = [this] { saveCurrentSettings(); };
    saveSettingsButton.setTooltip("Remember this chain, audio device, and volume as the setup to "
                                   "load automatically the next time the app starts.");

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

    tabs.addTab("Pedals", ModernColours::background, &pedalList, false);
    tabs.addTab("Amps", ModernColours::background, &ampList, false);
    tabs.addTab("Cabs", ModernColours::background, &cabList, false);
    tabs.addTab("Lab", ModernColours::background, &labList, false);
    addAndMakeVisible(tabs);

    setSize(1150, 680);

    // Open with 0 input channels so nothing is captured/played until the
    // user picks their interface in Settings; output stays on the system
    // default so the device selector has something initialised to show.
    // If Save Settings was ever clicked, restore that saved device instead
    // of falling back to the default - this has to happen before the
    // chain/volume restore below, since opening the device is what first
    // establishes a sample rate for pedals to prepare() against.
    auto savedDeviceXml = PresetManager::loadSavedAudioDeviceState();
    setAudioChannels(0, 2, savedDeviceXml.get());

    deviceManager.addChangeListener(this);
    updateStatusLabel();

    float savedMasterVolume = 100.0f;
    if (PresetManager::loadSavedChainAndVolume(signalChain, savedMasterVolume))
    {
        masterVolumeSlider.setValue(savedMasterVolume, juce::dontSendNotification);
        masterVolumeGain.store(savedMasterVolume / 100.0f, std::memory_order_relaxed);
    }
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
    // Flushes denormals to zero for this scope. Several pedals (tone
    // stacks, reverbs, the power amp's sag/feedback state, any IIR
    // filter) are recursive - their internal state decays toward zero
    // during quiet passages and note decays but mathematically never
    // reaches it, eventually going denormal. x86 handles denormal floats
    // via a much slower microcode path, and with enough recursive state
    // decaying at once (the newest, heaviest pedal in the rack right now
    // stacks a tone stack, a shelf filter, and a full oversampled power
    // amp stage, several times the recursive state of most other single
    // pedals) that slowdown can be enough to miss the audio callback's
    // deadline - which is exactly what a real-time buffer underrun sounds
    // like: glitchy, stuttering, "robotic" artifacts, worst during
    // sustained/decaying notes rather than constantly. This guard was
    // missing from the callback entirely, so every pedal was exposed in
    // theory; fixing it here protects all of them, not just the one that
    // happened to surface it first.
    juce::ScopedNoDenormals noDenormals;

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
    saveSettingsButton.setBounds(topBar.removeFromRight(100).removeFromTop(28));
    topBar.removeFromRight(8);
    loadPresetButton.setBounds(topBar.removeFromRight(100).removeFromTop(28));
    topBar.removeFromRight(8);
    savePresetButton.setBounds(topBar.removeFromRight(100).removeFromTop(28));
    topBar.removeFromRight(8);
    newPresetButton.setBounds(topBar.removeFromRight(100).removeFromTop(28));
    topBar.removeFromRight(10);

    auto volumeArea = topBar.removeFromRight(200);
    masterVolumeLabel.setBounds(volumeArea.removeFromTop(16));
    masterVolumeSlider.setBounds(volumeArea.removeFromTop(24));

    statusLabel.setBounds(topBar);

    area.removeFromTop(10);

    // The rack lives in the top-right, sized like a real stage rack
    // column rather than stretched full-width - the pedal/amp/cab/lab
    // catalogs take the rest of the space on the left so browsing and
    // the rack are side by side instead of stacked.
    constexpr int rackWidth = 400;
    auto rackArea = area.removeFromRight(juce::jmin(rackWidth, area.getWidth()));
    area.removeFromRight(10);

    tabs.setBounds(area);
    signalChain.setBounds(rackArea);
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

void MainComponent::startNewPreset()
{
    // Nothing to lose in an empty rack: just leave it blank.
    if (signalChain.getPedalsInOrder().empty())
        return;

    auto* alertWindow = new juce::AlertWindow("New Preset",
                                               "Clear the rack and start from a blank slate? Anything you haven't "
                                               "saved as a preset will be lost.",
                                               juce::AlertWindow::WarningIcon);
    alertWindow->addButton("Clear Rack", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(true, juce::ModalCallbackFunction::create([this](int result)
    {
        if (result == 1)
            signalChain.clear();
    }), true);
}

void MainComponent::openSavePresetDialog()
{
    // "Save Preset..." now offers a quick way to overwrite an existing
    // preset (this pedal has been getting tweaked all session - retyping
    // its exact name every time was the friction being reported) as well
    // as starting a brand new one.
    auto names = PresetManager::listPresetNames();

    juce::PopupMenu menu;
    menu.addItem(1, "Save as New Preset...");

    if (! names.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Overwrite Existing");
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(i + 2, names[i]);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(savePresetButton),
                        [this, names](int chosenId)
    {
        if (chosenId == 1)
            openNewPresetNameDialog();
        else if (chosenId >= 2 && chosenId <= names.size() + 1)
            confirmOverwritePreset(names[chosenId - 2]);
    });
}

void MainComponent::openNewPresetNameDialog()
{
    auto* alertWindow = new juce::AlertWindow("Save Preset", "Enter a name for this preset:", juce::AlertWindow::NoIcon);
    alertWindow->addTextEditor("name", "My Preset");
    alertWindow->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, alertWindow](int result)
    {
        if (result == 1)
        {
            auto name = alertWindow->getTextEditorContents("name").trim();
            if (name.isNotEmpty())
                PresetManager::savePreset(signalChain, name);
        }
    }), true);
}

void MainComponent::confirmOverwritePreset(const juce::String& presetName)
{
    auto* alertWindow = new juce::AlertWindow("Overwrite Preset",
                                               "Overwrite \"" + presetName + "\" with the current chain? This can't be undone.",
                                               juce::AlertWindow::WarningIcon);
    alertWindow->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, presetName](int result)
    {
        if (result == 1)
            PresetManager::savePreset(signalChain, presetName);
    }), true);
}

void MainComponent::openLoadPresetMenu()
{
    auto names = PresetManager::listPresetNames();

    juce::PopupMenu menu;
    if (names.isEmpty())
        menu.addItem(1, "No presets saved yet", false);
    else
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(i + 1, names[i]);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(loadPresetButton),
                        [this, names](int chosenId)
    {
        if (chosenId >= 1 && chosenId <= names.size())
            PresetManager::loadPreset(signalChain, names[chosenId - 1]);
    });
}

void MainComponent::saveCurrentSettings()
{
    PresetManager::saveCurrentSettings(signalChain, deviceManager, (float) masterVolumeSlider.getValue());

    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Settings Saved",
                                            "This chain, audio device, and volume will now load automatically "
                                            "the next time the app starts.");
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
