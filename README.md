# C-Sim

A modular guitar amp/pedal rack builder built with [JUCE](https://juce.com/). Rather than picking from a handful of fixed amp presets, C-Sim lets you build a rig from individual circuit-level DSP blocks — preamps, power amps, cabinets, and effects — stacked and reordered freely in a literal 19" rack, and hear the result in real time against your own guitar input.

## Features

- **19" rack signal chain** — every block in your chain renders as a real rack-mounted unit (LED, live VU meter, drag-and-drop or button-based reordering) in a dedicated column, separate from the catalog of blocks you can add.
- **Preamp and power amp modeled as distinct blocks**, not lumped into one "amp" — pair any preamp with a Power Amp block (Tube or Solid-State) to get independent preamp-breakup vs. power-amp-breakup character, the way a real rig actually works.
- **Researched amp models**, each built from published schematics/circuit analysis rather than guessed: Peavey 5150/6505 lead channel, Mesa Boogie Dual/Triple Rectifier "Modern" channel, Fortin Meshuggah (JCM800 + "Jose mod" with a diode-clipping master stage), Diezel VH4 Mega/Lead channel, and a Mesa/Diezel hybrid that splices specific stages from both.
- **Cabinet system** — procedurally synthesized cab + mic + room impulse responses (4 cab types, Dynamic/Ribbon mic types with continuous position blending, Live/Studio room mic, an Air control) run through real convolution, or load your own captured `.wav` IR to override the synthetic one.
- **Effects**: reverb, spring reverb, noise gate, transient shaper, graphic EQ, chorus/doubler, pitch transpose, drive/distortion (including a Tube Screamer-style Precision Drive clone), and a frequency-targeted dynamic EQ (Palm Mute Tamer) for taming palm-mute low-mid buildup without touching anything else.
- **Lab tab** — the raw circuit-level building blocks (triode gain stage, passive tone stack, diode clipper, power amp, dynamic gain stage, bias-modulated tremolo, spring reverb) for building your own amp from scratch instead of using a preset.
- **Presets and settings**: named presets (save/load full chains), plus a one-click "Save Settings" that remembers your whole rig (chain, audio device, master volume) and restores it automatically on the next launch.
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

Peavey 5150/6505, Mesa Boogie Dual/Triple Rectifier, Fortin Meshuggah, and Diezel VH4 are trademarks of their respective owners. This project is an independent, non-commercial circuit-modeling exercise and is not affiliated with or endorsed by any of them.
