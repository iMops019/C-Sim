#pragma once

// The mixing math behind the Cabinet pedal's dual-cab mode, kept
// framework-agnostic (no JUCE) so it can be tested in isolation like the
// rest of this toolkit. The convolution itself lives in CabinetPedal;
// this is only "how much of each cab reaches each side" and "what does
// flipping a mic's polarity do to the blend".
//
// Two cabs, one on each side. Each output channel takes a PRIMARY cab (the
// one sitting on that side: Cab on the left, Cab R on the right) and a
// SECONDARY one (the other side's cab), weighted by Spread:
//   Spread = 0   -> primary and secondary weighted equally: both outputs
//                   are the same 50/50 blend of the two cabs (both cabs
//                   "centered" - the case where two different cab
//                   responses sum and interfere, a real comb filter).
//   Spread = 1   -> primary only: hard left/right, no interaction at all.
// The weights always sum to 1, so overall level doesn't jump as Spread
// moves (two DIFFERENT cabs summing coherently could still add or cancel
// at individual frequencies - that's the point, not a level bug).
namespace DualCabMix
{
    struct Weights
    {
        float primary;
        float secondary;
    };

    inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    inline Weights weightsForSpread(float spread01) noexcept
    {
        auto s = clamp01(spread01);
        return { 0.5f * (1.0f + s), 0.5f * (1.0f - s) };
    }

    // One cab's two mics, blended (0 = all Mic A, 1 = all Mic B), each with
    // an optional polarity flip. Inverting one mic of a blend is one of the
    // oldest tricks in studio miking: correlated content (the fundamental
    // and low end both mics hear the same way) cancels, so the blend goes
    // thin/hollow, while what differs between the mics stays.
    inline float mixMics(float micA, float micB, float blend01, bool invertA, bool invertB) noexcept
    {
        auto blend = clamp01(blend01);
        return (invertA ? -micA : micA) * (1.0f - blend) + (invertB ? -micB : micB) * blend;
    }
}
