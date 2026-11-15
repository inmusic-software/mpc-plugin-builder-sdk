# Acid Box

A monophonic acid bass synth plugin for Akai MPC standalone hardware, by RS.

It is inspired by the classic acid bass line machines: a single saw or square oscillator into a resonant 18 dB/oct ladder low-pass filter with a decaying filter envelope, accent and slide. It is an original implementation written for this SDK, not a copy of any hardware's circuit or firmware, and it is not affiliated with or endorsed by any hardware manufacturer.

Requires MPC OS 3.10.1 or later with External Plugin Support enabled (Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support).

## Playing it

- **Monophonic, last note wins.** Releasing a note while another is still held returns to the held note.
- **Accent:** a note with velocity at or above *Acc Thresh* is accented. It is louder, sweeps the filter deeper with slightly more resonance, and decays faster. Consecutive accented notes build up extra emphasis for about 300 ms.
- **Slide:** with the *Slide* switch on, a note that starts while the previous note is still held glides to the new pitch over *Slide Time* and does not retrigger the envelopes. Play overlapping (legato) notes to slide; use separated notes for normal retriggered notes. With *Slide* off every note retriggers.
- Pitch bend is ±2 semitones.

## Parameters

Page ACID:

| Name | Range | What it does |
|---|---|---|
| Cutoff | 60 – 8000 Hz | Base filter cutoff |
| Reso | 0 – 100 % | Filter resonance (self-oscillation is not intended; high values squelch) |
| Env Mod | 0 – 100 % | How far the filter envelope opens the cutoff (up to 5 octaves) |
| Decay | 30 – 2500 ms | Filter envelope decay (accented notes cap at 200 ms) |
| Accent | 0 – 100 % | Strength of accented notes |
| Drive | 0 – 100 % | Output saturation |
| Slide Time | 5 – 500 ms | Glide time for slid notes |
| Slide | on/off | Overlapping notes glide instead of retriggering |
| Wave | Saw / Square | Oscillator waveform |
| Volume | -40 – +6 dB | Master volume (parameter 0) |

Page MORE: Tune (±100 cents), Octave (-2 – +2), Acc Thresh (velocity 1 – 127), Gate Rel (3 – 500 ms amp release).

There are 8 built-in programs: Init Acid, Classic Line, Squelch, Square Bass, Sub Pump, Wide Open, Zap, Slidey.

## Notes and limitations

- The filter is a 4-pole ladder with the output taken after pole 3 and a tanh in the feedback path. It is tuned by ear to behave like an acid filter, not modelled component by component, so it will sound close in character but not identical to any specific machine.
- The filter runs at the host sample rate with no oversampling. At the highest resonance and cutoff settings it can sound slightly harsher than a hardware filter.
- No built-in sequencer: play it from an MPC sequence or pad/keyboard. Plugins cannot send MIDI back to the MPC.
- CPU: one voice, filter coefficients updated every 8 samples; expected to be a very small part of one core. This has been measured only on a desktop CPU with the host simulator, not on MPC hardware.

## Build

```
python tools/build.py "plugins/Acid Box"
python tools/package.py "plugins/Acid Box"
```

## Project layout

- `plugin.json` - manifest (name, manufacturer, uid). The `uid` must match the C++ `uniqueId`.
- `src/AcidBox.cpp` - the plugin source (parameters, MIDI handling, DSP).
- `ui.json` - which parameters appear on the MPC screen, in which order.
- `build/` - build output (the deployable `RS - VST - Acid Box` folder lives here).
- `dist/` - zips produced by `package.py`.
