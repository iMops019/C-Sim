#pragma once

#include "MarshallPlexiPowerAmp.h"
#include "MarshallPlexiPreamp.h"
#include "Oversampler4x.h"
#include "PickupLoading.h"

// The complete Marshall 1959HW (Plexi Super Lead 100): guitar volts in, speaker
// terminal volts out. It owns the preamp (MarshallPlexiPreamp.h: two channels, the
// passive mixer, V2, the cathode follower and the tone stack) and the power amp
// (MarshallPlexiPowerAmp.h: phase inverter, four EL34s, output transformer,
// speaker and the global feedback loop with Presence), and runs them in ONE
// oversampled loop because they are coupled sample by sample: the phase
// inverter's grid draws current from the tone stack's output node when it
// conducts, and the feedback loop closes from the speaker terminals back to the
// phase inverter.
//
// PICKUP. The jack's input impedance loads the guitar's pickup (Marshall's
// manual: the Low input is "darker due to its significantly lower input
// impedance"). The recording this app receives has already been through a pickup
// and an interface's ~1M input, so PickupLoading.h applies the CHANGE: nothing
// for the High jack (the drawing's 1M leak: a load at the reference is "no
// change"), a resonance-damping filter for the Low jack (68k in series with a 68k
// shunt: 136k). The type of guitar is a setting because the resonance depends on
// it; Off applies no correction, leaving the Low jack a plain 6dB pad.
//
// SPEAKER. The amp's feedback is taken at the speaker terminals, so it reacts to
// the speaker's impedance curve (a resonance peak near fs and a rising top).
// setSpeaker() takes the electrical model (the Cabinet publishes it); the
// cabinet's nominal impedance is taken to match the transformer tap the user
// selects (a matched load - the manual insists on it).
//
// Levels are volts throughout: 1.0 in is 1V at the guitar jack; the output is
// the voltage across the speaker (100W into 16 ohms peaks at ~57V).
class MarshallPlexi1959Amp
{
public:
    enum class Pickup { Off = 0, SingleCoil = 1, Humbucker = 2 };

    explicit MarshallPlexi1959Amp(double sampleRate);

    void setRouting(MarshallPlexiPreamp::Routing routing) noexcept;
    void setLowInput(bool useLowJack) noexcept;
    void setPickup(Pickup pickup) noexcept;

    // Knobs, rotation 0..1.
    void setVolumeI(float position) noexcept    { preamp.setVolumeI(position); }
    void setVolumeII(float position) noexcept   { preamp.setVolumeII(position); }
    void setTreble(float position) noexcept     { preamp.setTreble(position); }
    void setMiddle(float position) noexcept     { preamp.setMiddle(position); }
    void setBass(float position) noexcept       { preamp.setBass(position); }
    void setPresence(float position) noexcept   { power.setPresence(position); }

    // The transformer tap (4, 8 or 16 ohms) - and, matched, the cabinet.
    void setImpedanceTap(double ohms) noexcept;
    void setSpeaker(const AmpSpeakerLoad::Speaker& speaker) noexcept;

    void reset() noexcept;

    // Guitar volts in, speaker volts out.
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    MarshallPlexiPreamp& getPreamp() noexcept { return preamp; }
    MarshallPlexiPowerAmp& getPowerAmp() noexcept { return power; }

private:
    void configurePickup() noexcept;
    float processOversampledSample(float x) noexcept;

    Oversampler4x oversampler;
    double oversampledRate;
    MarshallPlexiPreamp preamp;
    MarshallPlexiPowerAmp power;
    PickupLoading pickupLoading;

    Pickup pickup = Pickup::Off;
    bool lowInput = false;
    double tapOhms = 16.0;
    AmpSpeakerLoad::Speaker speaker;
};
