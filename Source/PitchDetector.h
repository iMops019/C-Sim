#pragma once

// YIN pitch detection (de Cheveigne & Kawahara). Accurate monophonic pitch
// tracking - the standard approach used by most software guitar tuners.
namespace PitchDetector
{
    // Returns the detected fundamental frequency in Hz, or 0.0 if no
    // confident pitch could be found in this block of samples.
    double detectPitchYin(const float* samples, int numSamples, double sampleRate);
}
