// End-to-end check that the Mesa pedal's OR60 Presence and Resonance really
// apply the amp/speaker load model (dsp/AmpSpeakerLoad.h is tested against
// the physics on its own; this checks the wiring, through the real pedal):
//
//  1. with OR60 Off, Presence and Resonance do nothing at all (the Mesa path
//     is untouched);
//  2. with OR60 On, moving Presence changes the treble by exactly what the
//     model predicts at that frequency (and Resonance the bass), in the
//     right direction;
//  3. each knob leaves the other band alone.
//
// The load filter sits AFTER every nonlinearity in the pedal, so with the
// same input and the same non-load settings the pedal's fundamental at a
// given frequency scales by exactly the filter's gain there. Each
// measurement uses a FRESH pedal so the (sag-dependent) nonlinear stages
// follow identical trajectories, and the only thing that differs is the
// knob under test.

#include "../Source/MesaTripleRectifierPedal.h"
#include "../Source/dsp/AmpSpeakerLoad.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int blocks = 60;
    constexpr int measureSamples = 12000; // 0.25 s: 84 Hz -> 21 cycles, 4000 Hz -> 1000 cycles (whole cycles either way)

    PedalParameter* param(MesaTripleRectifierPedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    // Magnitude at EXACTLY freq over a whole number of cycles - see
    // CabSpeakerPushTest for why not the project's Goertzel helper.
    double exactMagnitude(const std::vector<float>& x, double freq)
    {
        std::complex<double> sum = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * M_PI * freq * static_cast<double>(n) / sampleRate);
        return std::abs(sum) * 2.0 / static_cast<double>(x.size());
    }

    // The pedal's output level at `freq` for one configuration, on a fresh pedal.
    double outputAt(double freq, bool orOn, float presence, float resonance)
    {
        MesaTripleRectifierPedal pedal;
        pedal.prepare(sampleRate, blockSize, 2);
        param(pedal, "OR60")->set(orOn ? 1.0f : 0.0f);
        param(pedal, "Presence")->set(presence * 100.0f);   // the knobs are 0..100; the arguments here are 0..1
        param(pedal, "Resonance")->set(resonance * 100.0f);

        std::vector<float> left, l(blockSize), r(blockSize);
        long index = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < blockSize; ++n, ++index)
                l[static_cast<size_t>(n)] = r[static_cast<size_t>(n)] =
                    static_cast<float>(0.05 * std::sin(2.0 * M_PI * freq * static_cast<double>(index) / sampleRate));
            float* channels[2] = { l.data(), r.data() };
            pedal.process(channels, 2, blockSize);
            left.insert(left.end(), l.begin(), l.end());
        }
        left.erase(left.begin(), left.end() - measureSamples);
        return exactMagnitude(left, freq);
    }

    double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

    // What the model says the filter does at `freq` for these knob settings,
    // relative to neutral - at the pre-warped frequency the digital filter
    // actually realizes (bilinear transform).
    double predictedDb(double freq, double presence, double resonance)
    {
        AmpSpeakerLoad::Speaker speaker;
        AmpSpeakerLoad::Amp amp;
        auto warped = 2.0 * sampleRate * std::tan(M_PI * freq / sampleRate);
        auto h = std::abs(AmpSpeakerLoad::analogResponse(speaker, amp, presence, resonance, warped));
        auto neutral = std::abs(AmpSpeakerLoad::analogResponse(speaker, amp, 0.5, 0.5, warped));
        return db(h / neutral);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    constexpr double bass = 84.0, treble = 4000.0;

    std::printf("=== OR60 Off: Presence and Resonance do nothing ===\n");
    {
        double worst = 0.0;
        for (double f : { bass, treble })
        {
            auto reference = outputAt(f, false, 0.5f, 0.5f);
            worst = std::max(worst, std::abs(db(outputAt(f, false, 0.0f, 0.0f) / reference)));
            worst = std::max(worst, std::abs(db(outputAt(f, false, 1.0f, 1.0f) / reference)));
        }
        std::printf("  output at 84 Hz and 4 kHz across Presence/Resonance 0..100%%: largest change %.6f dB\n", worst);
        check(worst < 1.0e-6, "the knobs are inert while OR60 is Off");
    }

    std::printf("\n=== OR60 On: the pedal applies the model ===\n");
    {
        auto neutralTreble = outputAt(treble, true, 0.5f, 0.5f);
        auto neutralBass = outputAt(bass, true, 0.5f, 0.5f);

        auto presenceUp = db(outputAt(treble, true, 1.0f, 0.5f) / neutralTreble);
        auto presenceDown = db(outputAt(treble, true, 0.0f, 0.5f) / neutralTreble);
        auto resonanceUp = db(outputAt(bass, true, 0.5f, 1.0f) / neutralBass);
        auto resonanceDown = db(outputAt(bass, true, 0.5f, 0.0f) / neutralBass);

        auto presenceUpPredicted = predictedDb(treble, 1.0, 0.5), presenceDownPredicted = predictedDb(treble, 0.0, 0.5);
        auto resonanceUpPredicted = predictedDb(bass, 0.5, 1.0), resonanceDownPredicted = predictedDb(bass, 0.5, 0.0);

        std::printf("  4 kHz vs neutral: Presence 100%% %+.2f dB (model %+.2f), Presence 0%% %+.2f dB (model %+.2f)\n",
                     presenceUp, presenceUpPredicted, presenceDown, presenceDownPredicted);
        std::printf("  84 Hz vs neutral: Resonance 100%% %+.2f dB (model %+.2f), Resonance 0%% %+.2f dB (model %+.2f)\n",
                     resonanceUp, resonanceUpPredicted, resonanceDown, resonanceDownPredicted);

        check(std::abs(presenceUp - presenceUpPredicted) < 0.15 && std::abs(presenceDown - presenceDownPredicted) < 0.15,
              "Presence moves the treble by what the model predicts (within 0.15 dB)");
        check(std::abs(resonanceUp - resonanceUpPredicted) < 0.15 && std::abs(resonanceDown - resonanceDownPredicted) < 0.15,
              "Resonance moves the bass by what the model predicts (within 0.15 dB)");
        check(presenceUp > 4.0 && presenceDown < -3.0, "Presence opens the top end up and tightens it down, by several dB");
        check(resonanceUp > 3.0 && resonanceDown < -3.0, "Resonance lets the bass bloom and tightens it, by several dB");

        std::printf("\n=== Each knob leaves the other band alone ===\n");
        auto presenceOnBass = db(outputAt(bass, true, 1.0f, 0.5f) / neutralBass);
        auto resonanceOnTreble = db(outputAt(treble, true, 0.5f, 1.0f) / neutralTreble);
        std::printf("  Presence 100%% at 84 Hz: %+.3f dB | Resonance 100%% at 4 kHz: %+.3f dB\n", presenceOnBass, resonanceOnTreble);
        check(std::abs(presenceOnBass) < 0.2 && std::abs(resonanceOnTreble) < 0.2, "Presence doesn't touch the bass, Resonance doesn't touch the treble");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
