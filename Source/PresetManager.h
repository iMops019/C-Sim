#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <memory>

#include "SignalChainComponent.h"

// Saves/loads the current Signal Chain - which pedals, in what order, and
// every parameter/bypass value - to/from a small JSON file per preset, in
// a Presets folder next to the app. Pedals are reconstructed via
// PedalCatalog::createByName() using the same string Pedal::getName()
// reports, so a preset stays loadable no matter which of the four tabs
// each pedal originally came from.
namespace PresetManager
{
    juce::File getPresetsDirectory();

    // Overwrites any existing preset with the same name.
    bool savePreset(SignalChainComponent& chain, const juce::String& presetName);

    // Replaces whatever's currently in `chain` with the saved preset. Any
    // saved pedal type this build no longer recognizes (or parameter name
    // it no longer has) is skipped rather than failing the whole load.
    bool loadPreset(SignalChainComponent& chain, const juce::String& presetName);

    juce::StringArray listPresetNames();

    // The one auto-loaded slot, separate from the named Presets above: the
    // whole rig as it stood when the user last clicked Save Settings -
    // Signal Chain, audio device selection, and master volume - written to
    // a single file in a Settings folder next to the app. Nothing here is
    // saved automatically on every knob tweak or on exit; it only changes
    // when the user explicitly asks to remember the current setup, so a
    // relaunch never silently overwrites it with an in-progress experiment.
    juce::File getSettingsDirectory();

    bool saveCurrentSettings(SignalChainComponent& chain, const juce::AudioDeviceManager& deviceManager,
                              float masterVolumePercent);

    // Read-only lookup of the saved audio device state, for handing to
    // AudioAppComponent::setAudioChannels() before the device opens - this
    // has to happen before the chain/volume below, since opening the
    // device is what first establishes a sample rate. Returns nullptr if
    // nothing has been saved yet.
    std::unique_ptr<juce::XmlElement> loadSavedAudioDeviceState();

    // Restores the saved chain into `chain` and writes the saved master
    // volume (0-150) into `masterVolumePercentOut`. Returns false, leaving
    // both untouched, if nothing has been saved yet.
    bool loadSavedChainAndVolume(SignalChainComponent& chain, float& masterVolumePercentOut);
}
