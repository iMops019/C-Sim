// End-to-end check of the Cabinet pedal's per-slot IR loaders, driving the
// REAL CabinetPedal (real juce::dsp::Convolution, real WAV files on disk).
// The pedal layer normally has no standalone test in this project (it's
// JUCE-coupled); this one exists because the IR path has a history of bugs
// that only showed up at runtime.
//
// The test IRs are built to be unmistakable in the output:
//   - "single":  one clean impulse. Through the pedal you get one clean
//                spike and almost no ringing.
//   - "echo":    an impulse plus a strong echo 120 samples later. The
//                echo shows up as a second spike at +120.
// A synthetic cab, by contrast, rings for tens of milliseconds - so
// "ringing after the main spike" tells the real IR from the built-in one.
//
// What it proves:
//   1. a real IR in a slot replaces the synthetic sound (and Blend/Room
//      get the "hear it cleanly" reset on the first load);
//   2. a Right slot switches dual-cab on by itself, and the two sides
//      carry DIFFERENT real IRs (dual cab + loaded IRs finally coexist);
//   3. nudging Type/Position no longer discards a loaded IR;
//   4. Clear puts the slot back on the synthetic cab;
//   5. a preset round trip (JSON text and all) restores the loaded slots;
//      a missing file falls back to the synthetic cab without crashing.

#include "CabPedalTestKit.h"

#include <cstdio>

namespace
{
    constexpr int echoDelay = 120;
}

using namespace CabTest;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("csim_cab_ir_test");
    dir.createDirectory();
    auto singleIR = makeIR(dir, "single.wav", { { 0, 1.0f }, { 30, 0.01f } });
    auto echoIR = makeIR(dir, "echo.wav", { { 0, 1.0f }, { echoDelay, 0.8f } });
    if (! singleIR.existsAsFile() || ! echoIR.existsAsFile())
    {
        std::printf("FAILED: could not write the test IR files\n");
        return 1;
    }

    // Everything after this uses default settings except where stated.
    std::printf("=== Baseline: the built-in synthetic cab rings ===\n");
    CabinetPedal pedal;
    prepared(pedal);
    {
        Rendered base;
        for (int i = 0; i < 4; ++i) // let the first synthetic IRs swap in
        {
            base = render(pedal);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        base = render(pedal);
        std::printf("  tail energy after the main spike: left %.2f, right %.2f\n", tailFraction(base.left), tailFraction(base.right));
        check(rings(base.left) && rings(base.right), "synthetic cab rings (so the measurement can tell it from a real IR)");
    }

    std::printf("\n=== Left Mic A: a real IR replaces the synthetic sound ===\n");
    pedal.loadImpulseResponseFile(0, singleIR);
    check(pedal.getImpulseResponseFileName(0) == "single.wav", "slot 0 reports its file name");
    check(param(pedal, "Mic Blend")->get() == 0.0f && param(pedal, "Room")->get() == 0.0f,
          "first load resets Mic Blend and Room to 0 (hear it cleanly)");
    {
        Rendered r;
        auto arrived = waitUntil(pedal, [&](const Rendered& x) { r = x; return isClean(x.left); });
        std::printf("  left tail %.3f, right tail %.3f\n", tailFraction(r.left), tailFraction(r.right));
        check(arrived, "left output becomes a clean single impulse (the real IR is in use)");
        check(isClean(r.right), "right output is the same real IR too (dual mode is still off)");
    }

    std::printf("\n=== Right Mic A: dual cab switches on, each side has its own real IR ===\n");
    pedal.loadImpulseResponseFile(2, echoIR);
    {
        Rendered r;
        auto arrived = waitUntil(pedal, [&](const Rendered& x) { r = x; return echoRatio(x.right, echoDelay) > 0.5; });
        auto leftEcho = echoRatio(r.left, echoDelay), rightEcho = echoRatio(r.right, echoDelay);
        std::printf("  echo at +%d samples: left %.2f, right %.2f (left tail %.3f)\n", echoDelay, leftEcho, rightEcho, tailFraction(r.left));
        check(arrived, "right side picks up the echo IR (Cab R was still 'Same as Cab')");
        check(leftEcho < 0.05 && isClean(r.left), "left side is untouched: still the single-impulse IR");
    }

    std::printf("\n=== Turning Type/Position no longer discards a loaded IR ===\n");
    param(pedal, "Position A")->set(90.0f);
    param(pedal, "Mic A")->set(1.0f);
    param(pedal, "Cab")->set(5.0f);
    {
        // Plenty of blocks + time for a resynth to have happened if it was going to.
        Rendered r;
        for (int i = 0; i < 6; ++i)
        {
            r = render(pedal);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        std::printf("  left tail %.3f after changing Cab/Mic A/Position A\n", tailFraction(r.left));
        check(isClean(r.left), "the left real IR survives knob changes");
    }
    param(pedal, "Position A")->set(30.0f);
    param(pedal, "Mic A")->set(0.0f);
    param(pedal, "Cab")->set(0.0f);

    std::printf("\n=== Clear returns the slot to the synthetic cab ===\n");
    pedal.clearImpulseResponse(0);
    check(pedal.getImpulseResponseFileName(0).isEmpty(), "slot 0 reports no file");
    // Blend is 0 (Mic A) so the left output is now Mic A's synthetic cab.
    {
        Rendered r;
        auto arrived = waitUntil(pedal, [&](const Rendered& x) { r = x; return rings(x.left); });
        std::printf("  left tail %.2f, right echo %.2f\n", tailFraction(r.left), echoRatio(r.right, echoDelay));
        check(arrived, "left side rings again (synthetic)");
        check(echoRatio(r.right, echoDelay) > 0.5, "right side still has its real echo IR");
    }

    std::printf("\n=== Presets: loaded slots survive a save/load round trip ===\n");
    pedal.loadImpulseResponseFile(0, singleIR);
    {
        auto state = pedal.getExtraState();
        check(! state.isVoid(), "loaded slots produce extra state to save");

        // Through actual JSON text, the way PresetManager stores it.
        auto text = juce::JSON::toString(state);
        auto parsed = juce::JSON::parse(text);

        CabinetPedal restored;
        prepared(restored);

        // A real preset restores the ordinary parameters (PresetManager sets
        // them before the pedal joins the chain) and THEN the extra state -
        // so Mic Blend/Room come back as saved (0/0 here, from the first
        // load's reset), not as a fresh pedal's defaults.
        for (auto* saved : pedal.getParameters())
            param(restored, saved->name.toRawUTF8())->set(saved->get());
        restored.setExtraState(parsed);
        check(restored.getImpulseResponseFileName(0) == "single.wav" && restored.getImpulseResponseFileName(2) == "echo.wav",
              "the restored pedal knows both files");

        Rendered r;
        auto arrived = waitUntil(restored, [&](const Rendered& x) { r = x; return isClean(x.left) && echoRatio(x.right, echoDelay) > 0.5; });
        std::printf("  restored: left tail %.3f, right echo %.2f\n", tailFraction(r.left), echoRatio(r.right, echoDelay));
        check(arrived, "the restored pedal actually plays them (left single, right echo)");

        CabinetPedal untouched;
        check(untouched.getExtraState().isVoid(), "a pedal with no loaded IRs saves nothing extra (old presets unchanged)");
    }

    std::printf("\n=== A missing file falls back to the synthetic cab ===\n");
    {
        juce::DynamicObject::Ptr obj(new juce::DynamicObject());
        juce::Array<juce::var> paths;
        paths.add(dir.getChildFile("does_not_exist.wav").getFullPathName());
        paths.add(juce::String());
        paths.add(juce::String());
        paths.add(juce::String());
        obj->setProperty("irFiles", paths);

        CabinetPedal orphan;
        prepared(orphan);
        orphan.setExtraState(juce::var(obj.get()));
        check(orphan.getImpulseResponseFileName(0).isEmpty(), "the missing slot reports no file");

        Rendered r;
        for (int i = 0; i < 4; ++i)
        {
            r = render(orphan);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        r = render(orphan);
        check(rings(r.left), "and simply plays the synthetic cab");
    }

    dir.deleteRecursively();
    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
