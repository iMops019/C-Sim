#pragma once

#include <cmath>

#include "MarshallToneStack.h"
#include "NodalNetwork.h"
#include "Oversampler4x.h"
#include "TriodeSection.h"

// The Marshall 1959HW (Plexi Super Lead 100) preamp, traced from the 1959's
// own drawings and checked against Marshall's 1959HW owner's manual. Input
// jack to the phase inverter's grid; the phase inverter, the four EL34s and
// the negative-feedback loop that carries Presence are separate blocks.
//
// SOURCES (all image-only PDFs, read visually at 300-600 dpi):
//  - "Marshall 1959", Unicord (Gulf+Western), July 1970, drawing 70-6-11 - the
//    primary source. Its preamp matches what the 1959HW manual describes as the
//    reissue's "1969 circuit": V2-A carries a cathode bypass cap the later
//    drawings lack, and the supply is 50+50uF.
//  - "100 Watt Amplifier Type #2 c.1967": hand-drawn, earlier; no V2-A bypass,
//    250pF treble cap. Its printed supply nodes (+270V V1, +300V V2, +375V
//    phase inverter) set the operating points here.
//  - Marshall "1959 STD" (1988, drawing 1959PRE.DGM): the later production
//    values, used to cross-check the topology.
//
// THE CIRCUIT. Two channels share one 12AX7 (V1) and one passive mixer:
//
//   Channel I  "High Treble":  V1 half with a 2.7k cathode resistor bypassed by
//     only .68uF (a bass shelf-down below ~90-460Hz), .0022uF coupling (a
//     high-pass near 70Hz into the 1M Volume pot), a .005uF bright cap across
//     the Volume pot's upper half, and a 470k||500pF into the mixer.
//   Channel II "Normal": V1's other half, 820 ohm fully bypassed (250uF),
//     .022uF coupling, 470k into the mixer. Darker and fuller than Channel I:
//     the 470k into V2-A's grid (with the tube's Miller capacitance) is a
//     ~4kHz low-pass that Channel I's 500pF bypasses.
//
//   The two Volume wipers meet at V2-A's grid through those 470ks (so they
//   load and mix each other - a linear network, solved as two superposed
//   NodalNetworks). V2-A (100k plate load, 820 ohm cathode with a .68uF bypass
//   - the extra bass shelf-down and upper-mid lift the manual's "Tonal Note 1"
//   describes) drives V2-B directly: a cathode follower (100k cathode), the
//   low-impedance source for the tone stack. Its grid sits at V2-A's plate
//   voltage, so it starts to conduct - and limit the positive swing of V2-A's
//   plate - at ~10-20V peak: the compression that makes a cranked Plexi feel
//   the way it does.
//
//   The Marshall tone stack (MarshallToneStack.h) then feeds the phase
//   inverter's grid through a .022uF coupling cap.
//
// JACKS. Each channel has a High and a Low input. Per the 1959HW manual, Low is
// 6dB down: it is the same 68k series resistor into a grid that a second 68k
// (through the unused High jack's shorting contact) shunts to ground - modeled
// here. (Manual: Low is also "darker due to its significantly lower input
// impedance": that is the guitar pickup being loaded harder - a 136k input against
// the High jack's ~1M - and is applied by MarshallPlexi1959Amp with PickupLoading.h,
// since this preamp's signal is a recording that has already been through a
// pickup.) Jumpering - a cable from Channel I's Low jack to Channel II's High
// jack - puts the same guitar on both channels.
//
// JUDGMENT CALLS (not on the drawings): the 12AX7's mu 100 / rp 62.5k datasheet
// numbers and the Koren curve; ~100pF of Miller capacitance at the 12AX7 grids
// (~40pF at the phase inverter's); the Volume and Bass pots' audio taper (1M
// "log", assumed 15% at 12 o'clock; only the Middle's 10% log is sourced); the
// grid-conduction knees; and reading the cathode follower's grid-conduction
// point off its cathode resistor (the follower bootstraps its own grid, so it
// takes ~30x the grid-cathode margin in signal). The July 1970 drawing prints
// no test voltages, so unlike the Super-Sonic 22 there is nothing here to
// calibrate against: the operating points are solved from the resistors and
// supply nodes, and the tests check them for self-consistency with the drawn
// supply droppers instead.
//
// Input and output are volts (1.0 = 1V at the jack).
class MarshallPlexiPreamp
{
public:
    enum class Routing { ChannelI = 0, ChannelII = 1, Jumpered = 2 };

    explicit MarshallPlexiPreamp(double sampleRate);

    void setRouting(Routing newRouting) noexcept;
    void setLowInput(bool useLowJack) noexcept; // Channel I's jack; the jumper always lands on Channel II's High

    // All knobs are rotation, 0..1 (7 o'clock to 5 o'clock, 12 o'clock = 0.5).
    void setVolumeI(float position) noexcept;   // High Treble
    void setVolumeII(float position) noexcept;  // Normal
    void setTreble(float position) noexcept;
    void setMiddle(float position) noexcept;
    void setBass(float position) noexcept;

    // V2-B, the cathode follower: its open-circuit output voltage for a
    // voltage on its grid (V2-A's plate, AC only). A gain just under 1 for the
    // negative swing; the positive swing is limited where the grid starts to
    // conduct. Exposed so the limiting can be checked directly.
    double followerTransfer(double plateVolts) const noexcept
    {
        auto limited = plateVolts > 0.0 ? followerKnee * std::tanh(plateVolts / followerKnee) : plateVolts;
        return followerGain * limited;
    }

    // Force V2-A's cathode bypass to this capacitance instead of the drawn
    // .68uF (0 restores it; use a huge value for "fully bypassed"). For tests:
    // it is how they prove the bypass really is in the signal path.
    void setV2aBypassFarads(double farads) noexcept;

    // Where the DC solver put each stage, and the cathode follower's knee.
    struct OperatingPoints
    {
        TriodeSection::DcPoint v1Normal, v1HighTreble, v2a, cathodeFollower;
        double followerKneeVolts = 0.0; // V2-A plate swing at which V2-B's grid starts to conduct
    };
    const OperatingPoints& getOperatingPoints() const noexcept { return operating; }

    // Peak volts at the interesting nodes since resetTestPoints(); tracked only
    // while enabled, for tests. Plates are the LOADED voltages (what a scope
    // would see); the grid and stack output are what the next tube sees.
    struct TestPoints
    {
        double plateI = 0.0, plateII = 0.0;    // the two V1 plates
        double v2aGrid = 0.0;                  // the mixer's output
        double v2aPlate = 0.0;                 // V2-A / the follower's grid
        double stackOutput = 0.0;              // the phase inverter's grid
    };
    void setTestPointsEnabled(bool enabled) noexcept { testPointsEnabled = enabled; }
    void resetTestPoints() noexcept { testPoints = {}; }
    const TestPoints& getTestPoints() const noexcept { return testPoints; }

    void reset() noexcept;

    // Volts in, volts out (the phase inverter's grid).
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    // The same thing one sample at a time at the OVERSAMPLED rate (4x the rate
    // given to the constructor), split at the tone stack's output node so a power
    // amp whose phase inverter loads that node can be solved in the same loop
    // (see MarshallPlexi1959Amp): processToStack() returns that node's voltage
    // with nothing drawn from it, stackOutputOhms() its resistance this sample,
    // and finishStack() takes the current the next stage drew and returns the
    // node's final voltage. Call refreshControls() once per block first.
    void refreshControls() noexcept;
    double processToStack(double inputVolts) noexcept;
    double stackOutputOhms() const noexcept { return stack.outputOhms(); }
    double finishStack(double ampsDrawn) noexcept;

private:
    struct Front
    {
        NodalNetwork net;
        int plateI = 0, plateII = 0, out = 0;
        int volIUpper = 0, volILower = 0, volIIUpper = 0, volIILower = 0;
    };

    void buildFront(Front& front, bool drivesChannelI);
    float processOversampledSample(float x) noexcept;
    void updateComponents() noexcept;
    void configureShelves() noexcept;
    void setKnob(float& slot, float position) noexcept;

    Oversampler4x oversampler;
    double sampleRate;
    double oversampledRate;

    TriodeSection::Section v1Normal, v1HighTreble, v2a;
    TriodeSection::CathodeShelf shelfHighTreble, shelfV2a;
    double v2aBypassOverride = 0.0;

    // The mixer, once with V1's Channel I plate as the source (Channel II's
    // grounded through its plate resistance) and once the other way round;
    // their outputs add, since the network is linear.
    Front frontI, frontII;
    MarshallToneStack stack;

    OperatingPoints operating;
    double followerGain = 1.0, followerKnee = 1.0;

    Routing routing = Routing::ChannelI;
    bool lowInput = false;
    bool dirty = true;
    float volumeI = 0.5f, volumeII = 0.5f, treble = 0.5f, middle = 0.5f, bass = 0.5f;

    bool testPointsEnabled = false;
    TestPoints testPoints;
};
