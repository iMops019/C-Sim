// Verify the top-bar peak meter's logic (the part that decides what the user is told):
//  1. the dB scale (1.0 = 0 dBFS, 0.5 = -6.02 dB, silence = the floor);
//  2. the bar jumps to a new peak at once and falls at 30 dB/s;
//  3. the peak-hold tick stays on the highest peak for 1.5 s, then falls at 20 dB/s
//     without ever dropping under the bar;
//  4. the CLIP flag latches when a block reaches full scale - not for a block just under
//     it - stays lit after the signal goes quiet, and only a click clears it;
//  5. the audio thread's pushPeak() keeps the MAXIMUM since the UI last looked, and is
//     safe with several threads pushing at once.

#include "../Source/LevelMeterComponent.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };
    auto near = [](double a, double b, double tol) { return std::abs(a - b) <= tol; };

    std::printf("=== The dB scale ===\n");
    check(near(LevelMeterComponent::toDb(1.0f), 0.0, 1e-4), "1.0 is 0 dBFS");
    check(near(LevelMeterComponent::toDb(0.5f), -6.0206, 1e-3), "0.5 is -6.02 dBFS");
    check(near(LevelMeterComponent::toDb(0.0f), LevelMeterComponent::floorDb, 1e-6), "silence is the floor");
    check(near(LevelMeterComponent::toDb(1.0e-9f), LevelMeterComponent::floorDb, 1e-6), "far below the floor clamps to it");

    std::printf("=== The bar ===\n");
    {
        LevelMeterComponent m("T");
        m.advance(1.0 / 30.0, 0.5f);
        check(near(m.getBarDb(), -6.02, 0.01), "a new peak is shown at once");
        for (int i = 0; i < 30; ++i) m.advance(1.0 / 30.0, 0.0f);
        check(near(m.getBarDb(), -6.02 - 30.0, 0.5), "then it falls 30 dB in a second");
        for (int i = 0; i < 300; ++i) m.advance(1.0 / 30.0, 0.0f);
        check(near(m.getBarDb(), LevelMeterComponent::floorDb, 1e-6), "and settles at the floor");
        m.advance(1.0 / 30.0, 0.1f);
        m.advance(1.0 / 30.0, 0.4f);
        check(near(m.getBarDb(), -7.96, 0.05), "a louder block replaces a quieter one");
    }

    std::printf("=== The peak hold ===\n");
    {
        LevelMeterComponent m("T");
        m.advance(1.0 / 30.0, 0.5f);
        for (int i = 0; i < 30; ++i) m.advance(1.0 / 30.0, 0.0f); // 1 s of silence
        check(near(m.getHeldDb(), -6.02, 0.01), "the tick has not moved after 1 s (holds for 1.5 s)");
        for (int i = 0; i < 30; ++i) m.advance(1.0 / 30.0, 0.0f); // 2 s in
        check(m.getHeldDb() < -6.02 - 1.0, "after the hold time it starts to fall");
        check(m.getHeldDb() >= m.getBarDb() - 1e-3f, "and never under the bar");
    }

    std::printf("=== The CLIP latch ===\n");
    {
        LevelMeterComponent m("T");
        m.advance(1.0 / 30.0, 0.998f);
        check(! m.isClipLatched(), "a block just under full scale (0.998) is not a clip");
        m.advance(1.0 / 30.0, 0.999f);
        check(m.isClipLatched(), "a block at full scale (0.999) latches CLIP");
        for (int i = 0; i < 600; ++i) m.advance(1.0 / 30.0, 0.0f);
        check(m.isClipLatched(), "and it stays lit long after the signal has gone quiet");
        m.clearClip();
        check(! m.isClipLatched(), "a click clears it");
        m.advance(1.0 / 30.0, 1.7f);
        check(m.isClipLatched(), "a block well over full scale (1.7) latches it again");
    }

    std::printf("=== pushPeak from the audio thread ===\n");
    {
        LevelMeterComponent m("T");
        m.pushPeak(0.2f);
        m.pushPeak(0.7f);
        m.pushPeak(0.4f);
        check(near(m.takePendingPeak(), 0.7, 1e-6), "keeps the maximum of several blocks");
        check(m.takePendingPeak() == 0.0f, "and is empty once taken");

        std::atomic<float> trueMax { 0.0f };
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&, t] {
                unsigned seed = 1234u + static_cast<unsigned>(t) * 77u;
                float mine = 0.0f;
                for (int i = 0; i < 100000; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    auto v = static_cast<float>(seed >> 8) / 16777216.0f;
                    m.pushPeak(v);
                    mine = std::max(mine, v);
                }
                auto current = trueMax.load();
                while (mine > current && ! trueMax.compare_exchange_weak(current, mine)) {}
            });
        for (auto& th : threads) th.join();
        check(m.takePendingPeak() == trueMax.load(), "with four threads pushing at once it still ends on the true maximum");
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
