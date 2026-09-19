// A developer tool, not a test (its name doesn't end in "Test", so the test
// loops skip it): renders the Cabinet's real Inspector window - the visual
// cab editor plus the knob grid and IR rows - OFFSCREEN to PNG files, so
// the layout can be looked at without running the app and clicking around.
//
//   EditorSnapshot <output directory>
//
// Each scenario sets some parameters, builds the Inspector for a fresh
// CabinetPedal, and writes <name>.png. The window is never shown.

#include "../Source/CabinetPedal.h"
#include "../Source/ModernLookAndFeel.h"
#include "../Source/PedalInspectorWindow.h"

#include <functional>
#include <cstdio>

namespace
{
    PedalParameter* param(CabinetPedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    bool snapshot(CabinetPedal& pedal, const juce::File& file)
    {
        PedalInspectorWindow window(pedal, [] {}, [] {});
        window.setVisible(false);

        auto* content = window.getContentComponent();
        if (content == nullptr)
            return false;

        auto image = content->createComponentSnapshot(content->getLocalBounds(), true, 1.0f);
        if (! image.isValid())
            return false;

        file.deleteFile();
        juce::FileOutputStream out(file);
        if (! out.openedOk())
            return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream(image, out);
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ModernLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    if (argc < 2)
    {
        std::printf("usage: EditorSnapshot <output directory>\n");
        return 2;
    }
    juce::File dir(juce::String::fromUTF8(argv[1]));
    dir.createDirectory();

    struct Scenario
    {
        const char* name;
        std::function<void(CabinetPedal&)> setup;
    };

    const Scenario scenarios[] = {
        { "01_default", [](CabinetPedal&) {} },
        { "02_dual_cab", [](CabinetPedal& p) { param(p, "Cab")->set(0.0f); param(p, "Cab R")->set(5.0f); param(p, "Spread")->set(80.0f); } },
        { "03_distance_and_mixed", [](CabinetPedal& p)
            {
                param(p, "Distance A")->set(10.0f);
                param(p, "Distance B")->set(90.0f);
                param(p, "Spk 2")->set(2.0f);
                param(p, "Spk Mix")->set(50.0f);
                param(p, "Spk Push")->set(70.0f);
            } },
        { "04_everything", [](CabinetPedal& p)
            {
                param(p, "Cab")->set(1.0f);
                param(p, "Cab R")->set(6.0f);
                param(p, "Spk 2")->set(3.0f);
                param(p, "Spk Mix")->set(40.0f);
                param(p, "Spk 2 R")->set(1.0f);
                param(p, "Spk Mix R")->set(60.0f);
                param(p, "Distance A")->set(20.0f);
                param(p, "Distance B")->set(75.0f);
                param(p, "Spk Push")->set(90.0f);
                param(p, "Mic Spread")->set(100.0f);
            } },
    };

    int failures = 0;
    for (auto& scenario : scenarios)
    {
        CabinetPedal pedal;
        pedal.prepare(48000.0, 512, 2);
        scenario.setup(pedal);

        auto file = dir.getChildFile(juce::String(scenario.name) + ".png");
        auto ok = snapshot(pedal, file);
        std::printf("%s %s\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8());
        failures += ok ? 0 : 1;
    }
    return failures == 0 ? 0 : 1;
}
