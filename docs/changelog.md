# Changelog

All notable changes to the MPC Plugin SDK are recorded here, newest first.

## Unreleased (will become 1.2)

### Added

- **`VERSION` file** at the SDK root holding the SDK version. `python tools/build.py --version`
  prints it, and every build logs it, so you can tell which SDK a plugin was built with.

## 1.1

### Added

- **MPC Sample support: ability to build Knob FX.** Effect plugins run on the MPC Sample (firmware
  v1.4.0 or later, *Custom Plug-In Support* on); instruments do not. An effect's `plugin.json`
  gains `shortName`, `knobs` and `shiftKnobs`: the 3 + 3 parameters on the MPC Sample's knobs,
  which are the plugin's whole UI there. The effect template and StereoDelay include a mapping,
  the validator checks and prints it, and the deploy skill installs through the MPC Sample's
  microSD card in SD Card Access mode. See `docs/knob-fx.md`.
- **Optional background image per UI page**: a `ui.json` page can name a PNG (`background`)
  from `assets/`, drawn over the background colour and behind the controls.
- **`ui.json` `qlinkGroups`** (`"columns"` default, or `"rows"`): Q-Link group orientation for
  the whole plugin.
- New validator checks: Q-Link rectangles that cut through a control, run off the tab area or sit
  unevenly (also for hand-written skins).

### Changed

- Q-Link group rectangles are now computed from each group's controls with an equal 10 px gap on
  every side, instead of one fixed tall rectangle on every page that cut into knobs and switches.
- `gen_ui.py` warns, naming each control and the kit widget to use instead, when a UI that uses
  kit widgets still has generic sliders. Long choice lists use `knob` with the overlay `menu`
  (themed). Templates scaffold kit widgets (`knob`, `fader`, `switch`); SimpleSynth and
  StereoDelay no longer use sliders. Guidance now says never to use the plain slider in a themed UI.
- Skills and `AGENTS.md` ask after a clean build whether you want a test install on a standalone MPC,
  and must wait for a yes before installing, updating or removing anything. A clean build or an
  earlier yes is not permission.

### Fixed

- `gen_ui.py` warns when a page `background` image is missing from `assets/`.

## 1.0

First public release. An SDK for building instrument and effect plugins for
Akai MPC standalone hardware from a plain-language description, using an AI coding agent
(Claude Code or Codex), and installing them directly onto the MPC. Requires MPC OS 3.10.1 or
later with *External Plugin Support* enabled. Runs on MPC X, X SE, Live, Live II, One, One+,
Key 61, Key 37, Force (32-bit) and MPC Live III, MPC XL and later (64-bit).

- **Framework** (`framework/`): header-only `mpc_plugin.h` (parameters, MIDI, transport, state,
  programs), `mpc_vst2_abi.h` (clean-room description of the VST 2.4 binary interface),
  `mpc_resources.h`, and `dsp/` building blocks: oscillators, biquad/SVF/one-pole filters, ADSR,
  LFO, delay line, noise, smoothing, voice allocation, tempo helpers, MIDI CC and WAV helpers.
- **Tools** (`tools/`): `build.py` (one-command cross-compile for both MPC CPU families, plus
  native, with Docker support), `host-sim` (command-line host that behaves like the MPC, for
  testing on your computer), `validate.py`, `gen_ui.py` (scripted UI from `ui.json`),
  `package.py`, `mpc_remote.py` (install and manage plugins on an MPC over the network),
  `new_plugin.py`, `make_ui_kit.py` and `make_knob.py` (film-strip knobs, faders, switches).
- **UI**: the `ui.json` scripted UI system with pages, Q-Link mapping and a `ui-kit/` of
  unipolar/bipolar film-strip knobs, faders and switches in several colour themes.
- **Examples** (`examples/`): `SimpleSynth` (8-voice subtractive synth with presets),
  `StereoDelay` (tempo-synced effect) and `DrumSynth` (multi-output drum instrument).
- **Templates** for new instrument and effect projects, and JSON Schemas for `plugin.json` and
  `ui.json`.
- **Docs** (`docs/`): plugin folder format, host contract, building, install and test, DSP
  cookbook, UI guide and reference, presets, troubleshooting, FAQ and legal notes.
- **Agent skills** (`.claude/skills/`, `.agents/skills/`): new, dsp, ui, build, test, package,
  presets, deploy and troubleshoot, plus `AGENTS.md` / `CLAUDE.md`.
