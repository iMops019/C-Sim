#include "PresetManager.h"
#include "PedalCatalog.h"

namespace PresetManager
{
    namespace
    {
        juce::File presetFile(const juce::String& presetName)
        {
            return getPresetsDirectory().getChildFile(juce::File::createLegalFileName(presetName) + ".json");
        }

        juce::File currentSettingsFile()
        {
            return getSettingsDirectory().getChildFile("current_settings.json");
        }

        juce::var buildChainVar(SignalChainComponent& chain)
        {
            juce::Array<juce::var> chainArray;

            for (auto* pedal : chain.getPedalsInOrder())
            {
                auto* paramsObj = new juce::DynamicObject();
                for (auto* param : pedal->getParameters())
                    paramsObj->setProperty(param->name, param->get());

                auto* entryObj = new juce::DynamicObject();
                entryObj->setProperty("type", pedal->getName());
                entryObj->setProperty("bypassed", pedal->bypassed.load());
                entryObj->setProperty("params", juce::var(paramsObj));

                auto extra = pedal->getExtraState();
                if (! extra.isVoid())
                    entryObj->setProperty("extra", extra);

                chainArray.add(juce::var(entryObj));
            }

            return chainArray;
        }

        // Replaces whatever's currently in `chain` with `chainVar` (as
        // produced by buildChainVar above). Any saved pedal type this build
        // no longer recognizes (or parameter name it no longer has) is
        // skipped rather than failing the whole load. `logContext` names
        // the preset/settings file in the skip-warning, for whichever
        // caller is loading.
        void applyChainVar(SignalChainComponent& chain, const juce::var& chainVar, const juce::String& logContext)
        {
            chain.clear();

            for (auto& entryVar : *chainVar.getArray())
            {
                auto type = entryVar.getProperty("type", juce::var()).toString();
                auto pedal = PedalCatalog::createByName(type);
                if (pedal == nullptr)
                {
                    juce::Logger::writeToLog(logContext + ": skipping unknown pedal type \"" + type + "\"");
                    continue;
                }

                auto paramsVar = entryVar.getProperty("params", juce::var());
                for (auto* param : pedal->getParameters())
                {
                    auto saved = paramsVar.getProperty(param->name, juce::var());
                    if (! saved.isVoid())
                        param->set(static_cast<float>(static_cast<double>(saved)));
                }

                pedal->bypassed.store(static_cast<bool>(entryVar.getProperty("bypassed", false)));

                // Extra (non-parameter) state needs a prepared pedal, so it
                // goes in AFTER addPedal() - which is what prepares it.
                auto* rawPedal = pedal.get();
                chain.addPedal(std::move(pedal));
                rawPedal->setExtraState(entryVar.getProperty("extra", juce::var()));
            }
        }
    }

    juce::File getPresetsDirectory()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getParentDirectory()
                       .getChildFile("Presets");
        dir.createDirectory();
        return dir;
    }

    juce::File getSettingsDirectory()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getParentDirectory()
                       .getChildFile("Settings");
        dir.createDirectory();
        return dir;
    }

    bool savePreset(SignalChainComponent& chain, const juce::String& presetName)
    {
        auto* rootObj = new juce::DynamicObject();
        rootObj->setProperty("name", presetName);
        rootObj->setProperty("chain", buildChainVar(chain));

        return presetFile(presetName).replaceWithText(juce::JSON::toString(juce::var(rootObj)));
    }

    bool loadPreset(SignalChainComponent& chain, const juce::String& presetName)
    {
        auto file = presetFile(presetName);
        if (! file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse(file);
        auto chainVar = parsed.getProperty("chain", juce::var());
        if (! chainVar.isArray())
            return false;

        applyChainVar(chain, chainVar, "Preset \"" + presetName + "\"");
        return true;
    }

    juce::StringArray listPresetNames()
    {
        juce::StringArray names;
        for (auto& f : getPresetsDirectory().findChildFiles(juce::File::findFiles, false, "*.json"))
            names.add(f.getFileNameWithoutExtension());

        names.sort(true);
        return names;
    }

    bool saveCurrentSettings(SignalChainComponent& chain, const juce::AudioDeviceManager& deviceManager,
                              float masterVolumePercent)
    {
        auto* rootObj = new juce::DynamicObject();
        rootObj->setProperty("chain", buildChainVar(chain));
        rootObj->setProperty("masterVolume", masterVolumePercent);

        if (auto deviceXml = deviceManager.createStateXml())
            rootObj->setProperty("audioDeviceXml", deviceXml->toString());

        return currentSettingsFile().replaceWithText(juce::JSON::toString(juce::var(rootObj)));
    }

    std::unique_ptr<juce::XmlElement> loadSavedAudioDeviceState()
    {
        auto file = currentSettingsFile();
        if (! file.existsAsFile())
            return nullptr;

        auto parsed = juce::JSON::parse(file);
        auto xmlString = parsed.getProperty("audioDeviceXml", juce::var()).toString();
        if (xmlString.isEmpty())
            return nullptr;

        return juce::XmlDocument::parse(xmlString);
    }

    bool loadSavedChainAndVolume(SignalChainComponent& chain, float& masterVolumePercentOut)
    {
        auto file = currentSettingsFile();
        if (! file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse(file);
        auto chainVar = parsed.getProperty("chain", juce::var());
        if (! chainVar.isArray())
            return false;

        applyChainVar(chain, chainVar, "Saved settings");
        masterVolumePercentOut = static_cast<float>(static_cast<double>(parsed.getProperty("masterVolume", 100.0)));
        return true;
    }
}
