#pragma once

// Shared harness for the tests that drive the REAL CabinetPedal (real
// juce::dsp::Convolution, real WAV files): render an impulse through the
// pedal, measure what comes out, and wait for the convolution engines'
// asynchronous IR loads to land. Used by CabPedalIRTest and
// CabMicPlacementTest.

#include "../Source/CabinetPedal.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace CabTest
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int windowBlocks = 12; // 6144 samples - longer than a synthetic cab's ~3840-sample IR

    inline juce::File makeIR(const juce::File& dir, const juce::String& name, std::vector<std::pair<int, float>> taps)
    {
        auto file = dir.getChildFile(name);
        file.deleteFile();

        juce::AudioBuffer<float> buffer(1, 512);
        buffer.clear();
        for (auto& tap : taps)
            buffer.setSample(0, tap.first, tap.second);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 1, 32, {}, 0));
        if (writer != nullptr)
        {
            stream.release(); // the writer owns it now
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        }
        return file;
    }

    struct Rendered
    {
        std::vector<float> left, right;
    };

    // Feeds one stereo impulse through the pedal and collects the tail,
    // then flushes with silence so the next call starts clean.
    inline Rendered render(CabinetPedal& pedal)
    {
        Rendered r;
        std::vector<float> l(blockSize), rr(blockSize);
        for (int b = 0; b < windowBlocks; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(rr.begin(), rr.end(), 0.0f);
            if (b == 0)
                l[0] = rr[0] = 1.0f;

            float* channels[2] = { l.data(), rr.data() };
            pedal.process(channels, 2, blockSize);
            r.left.insert(r.left.end(), l.begin(), l.end());
            r.right.insert(r.right.end(), rr.begin(), rr.end());
        }

        // Flush the convolution tail and filter states.
        for (int b = 0; b < windowBlocks; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(rr.begin(), rr.end(), 0.0f);
            float* channels[2] = { l.data(), rr.data() };
            pedal.process(channels, 2, blockSize);
        }
        return r;
    }

    inline size_t peakIndex(const std::vector<float>& x)
    {
        size_t best = 0;
        for (size_t n = 1; n < x.size(); ++n)
            if (std::abs(x[n]) > std::abs(x[best]))
                best = n;
        return best;
    }

    // Fraction of the signal's energy that arrives more than 20 samples
    // after its main spike - i.e. how much it "rings".
    inline double tailFraction(const std::vector<float>& x)
    {
        auto p = peakIndex(x);
        double total = 0.0, tail = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
        {
            auto e = static_cast<double>(x[n]) * x[n];
            total += e;
            if (n > p + 20)
                tail += e;
        }
        return total > 0.0 ? tail / total : 0.0;
    }

    // Size of the spike `delay` samples after the main one, relative to it.
    inline double echoRatio(const std::vector<float>& x, int delay)
    {
        auto p = peakIndex(x);
        double best = 0.0;
        for (int d = delay - 3; d <= delay + 3; ++d)
            if (p + static_cast<size_t>(d) < x.size())
                best = std::max(best, static_cast<double>(std::abs(x[p + static_cast<size_t>(d)])));
        return best / std::max(1.0e-12, static_cast<double>(std::abs(x[p])));
    }

    template <typename Predicate>
    bool waitUntil(CabinetPedal& pedal, Predicate&& ok)
    {
        for (int attempt = 0; attempt < 250; ++attempt)
        {
            if (ok(render(pedal)))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    inline PedalParameter* param(CabinetPedal& pedal, const char* name)
    {
        for (auto* p : pedal.getParameters())
            if (p->name == name)
                return p;
        return nullptr;
    }

    // "Clean" = one spike, almost no ringing (a real single-impulse IR);
    // "rings" = a synthetic cab.
    inline bool isClean(const std::vector<float>& x)  { return tailFraction(x) < 0.05; }
    inline bool rings(const std::vector<float>& x)    { return tailFraction(x) > 0.30; }

    inline void prepared(CabinetPedal& pedal)
    {
        pedal.prepare(sampleRate, blockSize, 2);
    }}
