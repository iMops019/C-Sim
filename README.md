# C-Sim

A modular guitar amp/pedal rack builder built with [JUCE](https://juce.com/). Rather than picking from a handful of fixed amp presets, C-Sim lets you build a rig from individual circuit-level DSP blocks — preamps, power amps, cabinets, and effects — stacked and reordered freely in a literal 19" rack, and hear the result in real time against your own guitar input.

## Features

- **19" rack signal chain** — every block in your chain renders as a real rack-mounted unit (LED, live VU meter, drag-and-drop or button-based reordering) in a dedicated column, separate from the catalog of blocks you can add.
- **Preamp and power amp modeled as distinct blocks**, not lumped into one "amp" — pair any preamp with a Power Amp block (Tube or Solid-State) to get independent preamp-breakup vs. power-amp-breakup character, the way a real rig actually works.
- **Cabinet system** — procedurally synthesized cab + mic + room impulse responses (8 cab types, Dynamic/Ribbon mic types with continuous position blending, Live/Studio room mic, an Air control) run through real convolution, or load your own captured `.wav` IR to override the synthetic one. The visual cab editor shows an "IR loaded" status line and dims Mic B's marker whenever it currently has no audible effect (Mic Blend at 0%, which loading a real IR sets by default), so it's clear when a mic move won't do anything instead of it just looking broken.
- **Effects**: reverb, spring reverb, noise gate, transient shaper, graphic EQ, chorus/doubler, pitch transpose, drive/distortion (including a Tube Screamer-style Precision Drive clone), a frequency-targeted dynamic EQ (Palm Mute Tamer) for taming palm-mute low-mid buildup without touching anything else, JHS Morning Glory, MXR Dyna Comp, EarthQuaker Warden, and a two-knob Fuzz Chorus (Fuzz Face fuzz + a deliberately light/subtle CE-2-style chorus).
- **Lab tab** — the raw circuit-level building blocks (triode gain stage, passive tone stack, diode clipper, power amp, dynamic gain stage, bias-modulated tremolo, spring reverb) for building your own amp from scratch instead of using a preset.
- **Amps tab — two from-scratch builds, grown one real, sourced stage/knob at a time instead of being fixed presets:**
  - **Fender Style Amp (WIP)** splices real circuit facts from several different sourced schematics onto one amp: a Bass/Mid/Treble tone stack traced from the tweed Fender Bassman 5F6-A (the same passive network Marshall cloned into the JTM45), a Hi-Treble bright-boost switch from the Roland Jazz Chorus JC-120's analog op-amp-era preamp, and a Tight low-end switch modeling Fender Twin Reverb's real solid-state-rectifier/heavy-negative-feedback power amp character. Also has a 4-way Tube selector (12AX7/12AT7/12AU7/5751, real datasheet gain factors) and a lightweight built-in Drive+Tone boost stage in front of the amp.
  - **Mesa Triple Rectifier (WIP)** is a genuine "one real amp per knob" build for Metalcore/djent-style high gain: a real 3-stage Mesa "Modern channel" tube cascade (gain-dependent bright-cap voicing filter) terminated in a diode-clipping master stage for real high-gain saturation; **Low** models the actual Rectifier Select (tube vs. silicon diode) and Bold/Spongy switches traced from Mesa's own schematic; **Mid** traces the Peavey 5150/6505 tone stack's real mid-scoop network; **High** traces a DIY-published circuit clone of the Revv Generator's Purple channel Baxandall treble; **Depth** reuses Diezel VH4's real, documented 80Hz bass-restoration control.
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
- `Tests/` — one standalone console test executable per `dsp/` module, each verifying a real physical/audible claim (e.g. "push-pull cancels even harmonics," "Drive grows distortion," "a quiet in-band tone isn't reduced just for being in-band") rather than just "it builds and runs."

## Testing

Every `dsp/` module has a matching test target. Build and run them all:

```bash
cmake --build build --config Debug
```

Then run each `Tests/*.exe` produced under `build/Debug/` (or `build/` on non-Windows). Each prints its own pass/fail summary per check and returns a non-zero exit code if anything failed.

## Attribution

Fender Bassman/Twin Reverb, Roland Jazz Chorus, Mesa Boogie Dual/Triple Rectifier, Peavey 5150/6505, Revv Generator, Diezel VH4, JHS Morning Glory, MXR Dyna Comp, EarthQuaker Devices Warden, and Fuzz Face/CE-2 are trademarks of their respective owners. This project is an independent, non-commercial circuit-modeling exercise and is not affiliated with or endorsed by any of them.
