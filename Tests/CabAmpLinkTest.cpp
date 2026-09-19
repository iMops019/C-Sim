// End to end: a real CabinetPedal and a real Mesa pedal run together as a
// chain, and the Mesa's OR60 Resonance must act on THE SPEAKER THE CABINET
// IS MODELING (the link is SpeakerLoadLink.h; the model is AmpSpeakerLoad.h).
//
//  1. Mesa alone: the generic default speaker.
//  2. Mesa with a Cabinet behind it: the Cabinet's selected cab, exactly -
//     the Mesa's Resonance response matches the model for THAT speaker to
//     within 0.15 dB, and differs from the default one.
//  3. Changing the Cabinet's cab changes what the Mesa does.
//  4. A Cabinet that is removed, or stops processing (bypassed), stops
//     defining the load: the Mesa falls back to its default speaker.
//
// The load filter is the LAST thing in the Mesa pedal, after every
// nonlinearity, so with the same input the Mesa's fundamental at a given
// frequency scales by exactly the filter's gain there. Each Mesa
// measurement uses a fresh pedal, so the only difference is the speaker.

#include "../Source/CabinetPedal.h"
#include "../Source/MesaTripleRectifierPedal.h"
#include "../Source/SpeakerLoadLink.h"
#include "../Source/dsp/AmpSpeakerLoad.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <chrono>
#include <complex>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int blocks = 60;
    constexpr int measureSamples = 12000;
    constexpr double frequency = 84.0; // 21 whole cycles in the measuring window

    PedalParameter* param(Pedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    double exactMagnitude(const std::vector<float>& x, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return std::abs(sum) * 2.0 / static_cast<double>(x.size());
    }

    // The Mesa's output level at `frequency` with OR60 on. If `cabinet` is
    // given it is run behind the Mesa block by block, as it would be in the
    // rack, so it keeps publishing its speaker.
    double mesaLevel(float resonancePercent, CabinetPedal* cabinet)
    {
        MesaTripleRectifierPedal mesa;
        mesa.prepare(sampleRate, blockSize, 2);
        param(mesa, "OR60")->set(1.0f);
        param(mesa, "Resonance")->set(resonancePercent);

        std::vector<float> l(blockSize), r(blockSize), quietL(blockSize, 0.0f), quietR(blockSize, 0.0f), out;
        long index = 0;
        if (cabinet != nullptr)
        {
            float* quiet[2] = { quietL.data(), quietR.data() };
            cabinet->process(quiet, 2, blockSize); // publish before the Mesa's first block
        }
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < blockSize; ++n, ++index)
                l[static_cast<size_t>(n)] = r[static_cast<size_t>(n)] =
                    static_cast<float>(0.05 * std::sin(2.0 * M_PI * frequency * static_cast<double>(index) / sampleRate));
            float* channels[2] = { l.data(), r.data() };
            mesa.process(channels, 2, blockSize);
            out.insert(out.end(), l.begin(), l.end());

            if (cabinet != nullptr)
            {
                std::fill(quietL.begin(), quietL.end(), 0.0f);
                std::fill(quietR.begin(), quietR.end(), 0.0f);
                float* quiet[2] = { quietL.data(), quietR.data() };
                cabinet->process(quiet, 2, blockSize);
            }
        }
        out.erase(out.begin(), out.end() - measureSamples);
        return exactMagnitude(out, frequency);
    }

    double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

    // What Resonance 100% vs neutral does at `frequency` on a given speaker, per the model.
    double predictedDb(const AmpSpeakerLoad::Speaker& speaker)
    {
        AmpSpeakerLoad::Amp amp;
        auto warped = 2.0 * sampleRate * std::tan(M_PI * frequency / sampleRate);
        auto h = std::abs(AmpSpeakerLoad::analogResponse(speaker, amp, 0.5, 1.0, warped));
        auto neutral = std::abs(AmpSpeakerLoad::analogResponse(speaker, amp, 0.5, 0.5, warped));
        return db(h / neutral);
    }

    // Resonance 100% relative to neutral, measured through the pedal.
    double measuredBoostDb(CabinetPedal* cabinet)
    {
        return db(mesaLevel(100.0f, cabinet) / mesaLevel(50.0f, cabinet));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    const auto defaultSpeaker = AmpSpeakerLoad::Speaker {};
    const auto openBack = AmpSpeakerLoad::speakerForCab(2);
    const auto fender = AmpSpeakerLoad::speakerForCab(4);

    std::printf("=== Mesa alone: the generic default speaker ===\n");
    double aloneBoost = 0.0;
    {
        SpeakerLoadLink::clear();
        aloneBoost = measuredBoostDb(nullptr);
        std::printf("  Resonance 100%% at %.0f Hz: %+.2f dB (model for the default speaker %+.2f)\n", frequency, aloneBoost, predictedDb(defaultSpeaker));
        check(std::abs(aloneBoost - predictedDb(defaultSpeaker)) < 0.15, "with no Cabinet, the amp drives the default speaker");
    }

    std::printf("\n=== With a Cabinet behind it: the Cabinet's own speaker ===\n");
    double openBoost = 0.0, fenderBoost = 0.0;
    {
        CabinetPedal cabinet;
        cabinet.prepare(sampleRate, blockSize, 2);

        param(cabinet, "Cab")->set(2.0f); // open-back 2x12
        openBoost = measuredBoostDb(&cabinet);
        param(cabinet, "Cab")->set(4.0f); // Fender 1x10
        fenderBoost = measuredBoostDb(&cabinet);

        std::printf("  open-back 2x12: %+.2f dB (model %+.2f) | Fender 1x10: %+.2f dB (model %+.2f) | default speaker: %+.2f dB\n",
                     openBoost, predictedDb(openBack), fenderBoost, predictedDb(fender), aloneBoost);
        check(std::abs(openBoost - predictedDb(openBack)) < 0.15, "behind an open-back 2x12 the amp drives the open-back's speaker");
        check(std::abs(fenderBoost - predictedDb(fender)) < 0.15, "behind a Fender 1x10 it drives the Fender's");
        check(std::abs(fenderBoost - openBoost) > 0.2, "and changing the Cabinet's cab changes what the amp does");

        // Mixed speakers: the amp drives the blend, by the same Spk Mix.
        param(cabinet, "Cab")->set(2.0f);
        param(cabinet, "Spk 2")->set(5.0f);      // list: None, V30, Greenback, Open Back, Combo, Fender 1x10...
        param(cabinet, "Spk Mix")->set(100.0f);  // all Fender 1x10
        auto mixedBoost = measuredBoostDb(&cabinet);
        std::printf("  open-back with a Fender 1x10 mixed in 100%%: %+.2f dB (a plain Fender 1x10: %+.2f)\n", mixedBoost, fenderBoost);
        check(std::abs(mixedBoost - fenderBoost) < 0.15, "a mixed second speaker is what the amp drives (mix 100% = that speaker alone)");
    }

    std::printf("\n=== A Cabinet that goes away stops defining the load ===\n");
    {
        // Removed: destroying the Cabinet clears the link immediately.
        {
            CabinetPedal cabinet;
            cabinet.prepare(sampleRate, blockSize, 2);
            param(cabinet, "Cab")->set(4.0f);
            float quiet[blockSize] = {};
            float* channels[2] = { quiet, quiet };
            cabinet.process(channels, 2, blockSize);
        }
        auto afterRemoval = measuredBoostDb(nullptr);
        std::printf("  after the Cabinet is removed: %+.2f dB (default speaker %+.2f)\n", afterRemoval, aloneBoost);
        check(std::abs(afterRemoval - aloneBoost) < 0.05, "a removed Cabinet is forgotten at once - back to the default speaker");

        // Bypassed: it simply stops publishing; after 250 ms the amp lets go.
        CabinetPedal cabinet;
        cabinet.prepare(sampleRate, blockSize, 2);
        param(cabinet, "Cab")->set(4.0f);
        {
            float quiet[blockSize] = {};
            float* channels[2] = { quiet, quiet };
            cabinet.process(channels, 2, blockSize); // published...
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(350)); // ...then goes quiet (bypassed)
        auto afterBypass = measuredBoostDb(nullptr);
        std::printf("  350 ms after the Cabinet stopped processing: %+.2f dB (default speaker %+.2f)\n", afterBypass, aloneBoost);
        check(std::abs(afterBypass - aloneBoost) < 0.05, "a Cabinet that stops processing (bypassed) stops defining the load");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
