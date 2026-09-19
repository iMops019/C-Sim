# C-Sim

A modular guitar amp/pedal rack builder built with [JUCE](https://juce.com/). Rather than picking from a handful of fixed amp presets, C-Sim lets you build a rig from individual circuit-level DSP blocks — preamps, power amps, cabinets, and effects — stacked and reordered freely in a literal 19" rack, and hear the result in real time against your own guitar input.

## Features

- **19" rack signal chain** — every block in your chain renders as a real rack-mounted unit (LED, live VU meter, drag-and-drop or button-based reordering) in a dedicated column, separate from the catalog of blocks you can add.
- **Preamp and power amp modeled as distinct blocks**, not lumped into one "amp" — pair any preamp with a Power Amp block (Tube or Solid-State) to get independent preamp-breakup vs. power-amp-breakup character, the way a real rig actually works.
- **Cabinet system** — procedurally synthesized cab + mic + room impulse responses (8 cab types, Dynamic/Ribbon mic types with continuous position blending, Live/Studio room mic, an Air control) run through real convolution. Built out well beyond a single cab:
  - **Dual cab, left/right** — a second cabinet (**Cab R**) on the right side with a **Spread** control: 0 sums both cabs to the center (they interfere, like real cabs do), 100 is hard left/right. "Same as Cab" (the default) leaves it off, at no extra CPU.
  - **Four independent IR loaders** — Left/Right × Mic A/B. Any slot can be swapped for a real captured `.wav` (each with its own **Load…/Clear** in the Inspector) while the others stay synthetic, works alongside dual cab, survives knob changes, and the file paths are saved in presets (a file that has moved quietly falls back to the synthetic cab).
  - **Mic distance** — **Distance A/B** (1–18 in) model both the *proximity effect* (a first-order bass shelf, stronger for a ribbon, matching the textbook formula) and the *arrival-time* offset between the two mics, which is what makes a real close+far blend sound full and phasey. Plus a **Polarity** flip per mic.
  - **Mixed speakers** — a second speaker type (**Spk 2** / **Spk Mix**, per cab) blended into the cab, e.g. a 4x12 with V30 bite and Greenback warmth.
  - **Speaker push** — **Spk Push** adds level-dependent speaker behavior a static IR can't have: cone-excursion bass saturation (odd harmonics), slow power compression, and per-speaker headroom derived from rated power (a 25 W Greenback breaks up before a 60 W V30). Off (0) is an exact bypass. It also works on real loaded IRs.
  - **Stereo done properly** — the mic is a centered, mono-safe source (mono sum has no notch) and all width comes from a decorrelated stereo room pair and the **Width** control. **Mic Spread** pans the two mics apart (Mic A left, Mic B right, the way an engineer pans a mic pair) for genuine width with no room at all; 0 (the default) is unchanged.
  - **A visual editor for all of it** — the drawn cab shows one cab, or two side by side in dual mode; speakers are tinted by type so a mixed cab shows its mix, with a heat glow for Spk Push. Mic markers drag in two dimensions: horizontally is position on the cone, vertically is **distance** (the marker shrinks as it moves away). The discrete choices (Right cab, 2nd speaker, and the right cab's 2nd speaker) are real dropdowns with mix sliders instead of numeric knobs, greyed out when they don't apply. A status line and a dimmed marker explain when moving a mic would do nothing (Mic B at 0% blend, or a slot holding a real IR).
- **Effects**: reverb, spring reverb, noise gate, transient shaper, graphic EQ, chorus/doubler, pitch transpose, drive/distortion (including a Tube Screamer-style Precision Drive clone and **Centaur Drive**, a Klon Centaur-style transparent overdrive: a mid-hump gain stage into a germanium clipper whose clean path recedes but never vanishes, plus a series **Light Distortion** hard-clip stage and a post-clip **Depth** low-end peak), a frequency-targeted dynamic EQ (Palm Mute Tamer) for taming palm-mute low-mid buildup without touching anything else, JHS Morning Glory, MXR Dyna Comp, EarthQuaker Warden, and a two-knob Fuzz Chorus (Fuzz Face fuzz + a deliberately light/subtle CE-2-style chorus).
- **Lab tab** — the raw circuit-level building blocks (triode gain stage, passive tone stack, diode clipper, power amp, dynamic gain stage, bias-modulated tremolo, spring reverb) for building your own amp from scratch instead of using a preset.
- **Amps tab — two from-scratch builds, grown one real, sourced stage/knob at a time instead of being fixed presets:**
  - **Fender Style Amp (WIP)** splices real circuit facts from several different sourced schematics onto one amp: a Bass/Mid/Treble tone stack traced from the tweed Fender Bassman 5F6-A (the same passive network Marshall cloned into the JTM45), a Hi-Treble bright-boost switch from the Roland Jazz Chorus JC-120's analog op-amp-era preamp, and a Tight low-end switch modeling Fender Twin Reverb's real solid-state-rectifier/heavy-negative-feedback power amp character. Also has a 4-way Tube selector (12AX7/12AT7/12AU7/5751, real datasheet gain factors) and a lightweight built-in Drive+Tone boost stage in front of the amp.
  - **Mesa Triple Rectifier (WIP)** is a genuine "one real amp per knob" build for Metalcore/djent-style high gain: a real 3-stage Mesa "Modern channel" tube cascade (gain-dependent bright-cap voicing filter) terminated in a diode-clipping master stage for real high-gain saturation; **Low** models the actual Rectifier Select (tube vs. silicon diode) and Bold/Spongy switches traced from Mesa's own schematic; **Mid** traces the Peavey 5150/6505 tone stack's real mid-scoop network; **High** traces a DIY-published circuit clone of the Revv Generator's Purple channel Baxandall treble; **Depth** reuses Diezel VH4's real, documented 80Hz bass-restoration control.
    - **OR60 add-on (experimental)** — a switch that puts an Orange preamp in front of the Mesa (Off leaves the Mesa exactly as it was). The preamp is the **Orange Dual Terror**'s two channels traced from its PCB schematic: the punchy **Tiny Terror** circuit (a tiny 1 nF coupling cap strips bass before the gain stages) and the **Fat** channel (68 nF caps and a cathode follower keep the low end), each with the real Gain pot topology, a differential Tone control, and a post-splitter Volume. **OR Bright** is the OR60's 3-position Bright switch (shimmer / neutral / bite). **Presence** and **Resonance** model what those controls physically are — the amp's negative-feedback loop driving a guitar speaker's impedance curve (resonance peak + inductive rise): less feedback lets the load's curve show through, more tightens it. Flat at 50, about −5/+6 dB either way. The amp drives the speaker of whichever **Cabinet** is behind it in the rack (each cab type has its own resonance frequency, mixed speakers blend), falling back to a generic speaker if there's no Cabinet or it's bypassed — a subtle color (well under 1 dB) rather than a dramatic one. The OR60 itself has no published schematic (2025 amp), so its Bright/Presence/Resonance come from Orange's own control descriptions plus documented judgment calls, not a circuit trace.
- **Presets and settings**: named presets (save/load full chains, with a quick "overwrite an existing preset" option alongside starting a new one), plus a one-click "Save Settings" that remembers your whole rig (chain, audio device, master volume) and restores it automatically on the next launch.
- **Built-in tuner**.

## Building

Requires a C++17 compiler and CMake 3.22+. JUCE is vendored as a git submodule.

```bash
git clone --recurse-submodules https://github.com/iMops019/C-Sim.git
cd C-Sim
cmake -S . -B build
cmake --build build --config Debug
```

The app binary is produced at `build/CSim_artefacts/Debug/C-Sim.exe` (or the equivalent Release path). If you already cloned without `--recurse-submodules`, run `git submodule update --init --recursive` first.

## Project structure

- `Source/` — the JUCE application layer: UI components (`SignalChainComponent`, `PedalInspectorWindow`, `PedalListComponent`, `MainComponent`), the `Pedal` interface, and one `*Pedal` class per rack block wrapping either a `dsp/` module or JUCE's own filters/convolution directly.
- `Source/dsp/` — a framework-agnostic (no JUCE dependency) DSP toolkit: the actual circuit-modeling math (triode stages, tone stacks, oversampling, diode clippers, the researched amp cascades, etc.), kept independent of the app so each module can be unit tested in isolation.
- `Tests/` — standalone console test executables. Each `dsp/` module has one, verifying a real physical/audible claim (e.g. "push-pull cancels even harmonics," "Drive grows distortion," "the amp/speaker filter is exactly the bilinear transform of the closed-loop formula, and flat at its neutral setting") rather than just "it builds and runs." A few also drive the real, JUCE-coupled `CabinetPedal`/Mesa pedal end to end with real WAV files (`Cab*Test`, `MesaLoadTest`, using the shared `CabPedalTestKit.h`), since that layer has had bugs that only show up at runtime.

## Testing

Every `dsp/` module has a matching test target, plus the end-to-end pedal tests above. Build and run them all:

```bash
cmake --build build --config Debug
```

Then run each `*Test.exe` produced under `build/Debug/` (or `build/` on non-Windows). Each prints its own pass/fail summary per check and returns a non-zero exit code if anything failed. The pedal-level tests load IRs asynchronously and poll for them, so they take a few seconds each. `CabEditorTest` drives the visual editor's controls and mouse through JUCE's API (no window) and reads the parameters back.

`EditorSnapshot` (built alongside, but not a test) renders the Cabinet's Inspector window offscreen to PNGs, so a layout can be looked at without launching the app:

```bash
build/Debug/EditorSnapshot.exe some/output/folder
```

## Attribution

Fender Bassman/Twin Reverb, Roland Jazz Chorus, Mesa Boogie Dual/Triple Rectifier, Peavey 5150/6505, Revv Generator, Diezel VH4, JHS Morning Glory, MXR Dyna Comp, EarthQuaker Devices Warden, Fuzz Face/CE-2, and Orange (Dual Terror, OR60) are trademarks of their respective owners. The Orange Dual Terror circuit values were read from a community-archived copy of its PCB schematic ([d3vCr0w/orange_terror_schematics](https://github.com/d3vCr0w/orange_terror_schematics), whose maintainer notes they don't own the schematics); the schematic itself is not included in this repository. This project is an independent, non-commercial circuit-modeling exercise and is not affiliated with or endorsed by any of them.
