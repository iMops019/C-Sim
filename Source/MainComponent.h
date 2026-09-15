#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <memory>

#include "PedalListComponent.h"
#include "SettingsWindow.h"
#include "SignalChainComponent.h"
#include "TunerEngine.h"
#include "TunerWindow.h"

class MainComponent : public juce::AudioAppComponent,
                       private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void openSettings();
    void openTuner();
    void updateStatusLabel();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::TextButton settingsButton{"Settings"};
    juce::TextButton tunerButton{"Tuner"};
    juce::Label statusLabel;
    std::unique_ptr<SettingsWindow> settingsWindow;

    TunerEngine tunerEngine;
    std::unique_ptr<TunerWindow> tunerWindow;

    juce::Label masterVolumeLabel;
    juce::Slider masterVolumeSlider;
    std::atomic<float> masterVolumeGain { 1.0f };

    SignalChainComponent signalChain;
    PedalListComponent pedalList{signalChain};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
