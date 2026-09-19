#pragma once

#include <array>
#include <vector>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "NodalNetwork.h"
#include "Oversampler4x.h"

// The Fender Super-Sonic 22's preamp, traced from Fender's own service
// diagram "Super-Sonic 22, combined (schematic)", drawing 0078327000 rev A
// (thetubestore.com hosts a copy) - three sheets, the third a PCB layout.
// Every component value was read off that drawing and the whole thing is
// modeled as a real circuit: the passive networks between the tubes are
// solved as networks (NodalNetwork.h), so the pots load each other and the
// plates they hang off the way they do in the amp, instead of being a
// handful of EQ filters chosen by ear.
//
// THE CIRCUIT. Four 12AX7 sections (V1-A, V1-B, V2-A, V2-B), each with a
// 100k plate load and a 1.5k cathode resistor. V1-A is shared; then the
// signal takes one of two routes, switched by relays:
//
//  VINTAGE ("a Deluxe Reverb with a Fat switch"):
//   V1-A -> a blackface-style two-band stack (250pF treble cap, 100k slope
//   resistor, .1uF and .047uF bass caps, 250k treble and bass pots, a fixed
//   6.8k mid resistor) -> Volume -> V1-B -> V2-B -> a 100k/15k divider.
//   The Volume pot sits BEFORE V1-B, so Vintage breaks up by turning it up,
//   like the amp it is modeled on. The NORMAL/FAT switch changes how V1-B
//   couples into V2-B:
//    - NORMAL: a .022uF cap into a 3.3M/270k attenuator (about -26 dB) with
//      a 10pF bleed cap across the 3.3M, plus a 47pF bright cap across the
//      Volume pot.
//    - FAT: the attenuator is replaced by a 47k/120k divider (about -3 dB)
//      and V2-B gets a 470k feedback resistor from its plate back to its
//      input (R24) - so Fat is hotter AND its second stage runs with local
//      negative feedback (less distortion, lower output impedance). A 220pF
//      cap also shunts V1-A's plate, and the bright cap comes out.
//
//  BURN (the high-gain channel):
//   V1-A -> a 2.2nF coupling cap (a ~300Hz high-pass in front of everything -
//   this is what keeps the low end tight) -> 15k -> GAIN 1 -> V1-B ->
//   GAIN 2 -> V2-A -> V2-B -> a passive Treble/Bass/Mid stack (Marshall/
//   Fender-family, with a real mid pot and a 150pF/.15uF/.022uF cap set) ->
//   a 220k/680pF lowpass -> Volume. Note the tone stack comes AFTER the
//   fourth stage - it shapes the distortion rather than what feeds it.
//   Burn also changes the cathode bypass on V1-B and V2-B: 2.2uF in Vintage
//   (a ~50-160Hz bass shelf-down in those stages), 24.2uF in Burn (full bass
//   gain), so Burn is tight at the input but not thin in the gain stages.
//
// WHAT'S CALIBRATED. The service diagram prints the AC voltage at a set of
// test points for a 5mV/1kHz input, with a note that they may vary +/-20%.
// The Vintage numbers (188mV at V1-A's plate, 313mV at V1-B's, 630mV Normal
// / 1.36V Fat at V2-B's, 88mV after the divider) fall out of the traced
// circuit using only the tube's datasheet gain (mu 100, rp 62.5k, the
// schematic's 100k load: 61.5 per stage) - nothing was tuned to them. The
// Fat/Normal ratio, which depends entirely on the R24 feedback loop, lands
// within 0.1dB of the printed +6.7dB. The Burn readings were taken at pot
// positions described only as "9 o'clock" and "full CCW", so the model's
// pot-angle-to-resistance law (a standard exponential audio taper, sized by
// each pot's printed taper code: 15A/30A/B) decides how close they land. Two
// of the three Burn points land; the printed V2-A plate voltage (TP19) does
// not, and that discrepancy is unresolved (see the test). It may be tied to a
// consequence of the trace worth knowing about: Gain 2's wiper is shunted by
// a 10k (R38), so the pot does almost nothing until the last ~15% of its
// travel (harmonics at Gain 1 = 0.5: 0.025 at Gain 2 = 0, 0.033 at 0.5, 0.142
// at 1.0). That is what the circuit as drawn does - if it sounds wrong
// against the real amp, this is the first place to look.
//
// JUDGMENT CALLS (not on the schematic): rp = 62.5k (12AX7 datasheet, the
// project's usual number) and 100pF of grid Miller capacitance; the
// exponential pot taper; and the grid-conduction knee. The
// Fat feedback is solved as a static loop (see the .cpp) - the network in
// front of it keeps its full frequency response, the loop itself is the
// midband value.
//
// SCOPE. This is the preamp only: input jack to the channel's output node.
// The reverb, effects loop, phase inverter and 6V6 power stage are separate
// blocks. Input and output are volts (1.0 = 1V at the input jack) so the
// service diagram's numbers can be checked directly.
class SuperSonic22Preamp
{
public:
    enum class Channel { Vintage = 0, Burn = 1 };

    explicit SuperSonic22Preamp(double sampleRate);

    void setChannel(Channel newChannel) noexcept;
    void setFat(bool fatOn) noexcept; // Vintage's NORMAL/FAT switch

    // All knobs are rotation, 0..1 (7 o'clock to 5 o'clock, 12 o'clock = 0.5).
    // Vintage channel.
    void setVintageVolume(float position) noexcept;
    void setVintageTreble(float position) noexcept;
    void setVintageBass(float position) noexcept;
    // Burn channel.
    void setGain1(float position) noexcept;
    void setGain2(float position) noexcept;
    void setBurnTreble(float position) noexcept;
    void setBurnBass(float position) noexcept;
    void setBurnMid(float position) noexcept;
    void setBurnVolume(float position) noexcept;

    // The voltage at each of the diagram's test points, as peak volts seen
    // since resetTestPoints() - tracked only while enabled, for tests. These
    // are the LOADED plate voltages (what a meter reads), not the tubes'
    // open-circuit voltages.
    struct TestPoints
    {
        double v1aPlate = 0.0; // TP16
        double v1bPlate = 0.0; // TP18 / BURN1
        double v2aPlate = 0.0; // TP19 (Burn only)
        double v2bPlate = 0.0; // TP21 / FAT / BURN2
        double output = 0.0;   // TP23 (Vintage) / the Volume wiper (Burn)
    };
    // V2-B's static transfer in Vintage, exposed so the R24 feedback loop can
    // be checked directly: the loaded plate voltage as a function of the
    // voltage at the tube's grid, bare (Normal), and - for Fat - as a
    // function of the grid voltage the input network alone would deliver
    // (the loop then moves the real grid by h * plate).
    double v2bNormalTransfer(double gridVolts) const noexcept;
    double v2bFatTransfer(double networkGridVolts) const noexcept { return fatClosedLoop(networkGridVolts); }

    // Force V1-B's and V2-B's cathode bypass to this capacitance instead of
    // the channel's own (2.2uF Vintage, 24.2uF Burn); 0 restores the default.
    // For experiments and tests - it is how a test proves the shelf really is
    // in the signal path.
    void setCathodeBypassOverride(double farads) noexcept;

    void setTestPointsEnabled(bool enabled) noexcept { testPointsEnabled = enabled; }
    void resetTestPoints() noexcept { testPoints = {}; }
    const TestPoints& getTestPoints() const noexcept { return testPoints; }

    // The cathode bypass cap that is too small to hold the whole audio band:
    // below its corner the cathode resistor degenerates the stage (local
    // negative feedback), so the bass gain is lower. A first-order shelf.
    struct CathodeShelf
    {
        double alpha = 0.0, depth = 0.0, state = 0.0;
        void configure(double sampleRate, double bypassFarads);
        void reset() noexcept { state = 0.0; }
        double process(double x) noexcept
        {
            state += alpha * (x - state);
            return x - depth * state;
        }
    };

    void reset() noexcept;

    // input and output are volts (the output is the channel's output node,
    // i.e. what would feed the effects-loop mixer).
    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    // One triode section: the Koren curve plus what it takes to run it in
    // real volts - see the .cpp.
    struct Stage
    {
        KorenTriodeStage koren;
        double swingVolts = 1.0;   // grid volts at the Koren stage's unit input
        double scaleVolts = 1.0;   // plate volts per unit of Koren output (sets the small-signal gain)
        double clampVolts = 1.0;   // grid-conduction knee

        void configure(double gridBias, double plateVolts, double smallSignalGain);
        double process(double gridVolts) const noexcept; // open-circuit plate voltage
    };

    float processOversampledSample(float x) noexcept;
    void setKnob(float& slot, float position) noexcept;
    void updateComponents() noexcept;
    void configureCathodeShelves() noexcept;
    double fatClosedLoop(double gridVolts) const noexcept;
    void buildFatTable();
    void resetNetworks() noexcept;

    Oversampler4x oversampler;
    double sampleRate;
    double oversampledRate;

    Stage v1a, v1b, v2a, v2b;
    CathodeShelf shelfV1B, shelfV2B;
    CouplingHighpass vintageOutputHighpass; // C14 into the Vintage load

    // Vintage front end: V1-A's plate -> stack -> Volume -> V1-B's grid.
    NodalNetwork vintageFront;
    int vfP = 0, vfOut = 0;
    int vfC3 = 0, vfC7 = 0, vfR9Upper = 0, vfR9Lower = 0, vfR10 = 0, vfR12Upper = 0, vfR12Lower = 0;

    // Burn front end: V1-A's plate -> C2 -> Gain 1 -> V1-B's grid. (The
    // Vintage stack stays connected to V1-A's plate in Burn, so it loads it.)
    NodalNetwork burnFront;
    int bfP = 0, bfOut = 0;
    int bfC3 = 0, bfR9Upper = 0, bfR9Lower = 0, bfR10 = 0, bfR31Upper = 0, bfR31Lower = 0;

    // V1-B -> V2-B, Vintage Normal and Vintage Fat.
    NodalNetwork interNormal, interFat;
    int inP = 0, inOut = 0, ifP = 0, ifOut = 0;

    // Burn: V1-B -> Gain 2 -> V2-A, then V2-A -> V2-B, then V2-B -> stack -> Volume.
    NodalNetwork burnGain2, burnInter, burnStack;
    int g2P = 0, g2Out = 0, g2R35Upper = 0, g2R35Lower = 0;
    int biP = 0, biOut = 0;
    int bsP = 0, bsOut = 0, bsR47Upper = 0, bsR47Lower = 0, bsR48 = 0, bsR49 = 0, bsR51Upper = 0, bsR51Lower = 0;

    // Fat's static feedback loop, tabulated: loaded V2-B plate volts as a
    // function of the grid voltage the network alone would produce.
    static constexpr int fatTableSize = 4097;
    std::vector<double> fatTable;
    double fatTableRange = 0.0;

    double cathodeOverrideFarads = 0.0;

    Channel channel = Channel::Vintage;
    bool fat = false;
    bool dirty = true;

    float vintageVolume = 0.5f, vintageTreble = 0.5f, vintageBass = 0.5f;
    float gain1 = 0.5f, gain2 = 0.5f, burnTreble = 0.5f, burnBass = 0.5f, burnMid = 0.5f, burnVolume = 0.5f;

    bool testPointsEnabled = false;
    TestPoints testPoints;
};
