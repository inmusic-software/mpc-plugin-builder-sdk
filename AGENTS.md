# AGENTS.md — instructions for AI coding agents

You are working inside the **MPC Plugin SDK**: a toolkit for building instrument and effect plugins that run on Akai MPC standalone hardware (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force, Live III, XL, and, for effects only, the MPC Sample). The person you are helping is probably not a C++ programmer. Your job is to take their description and deliver a plugin folder that works on their MPC.

## What a finished plugin is

A folder `"<Manufacturer> - VST - <Name>/"` containing `plugin.json`, `version.xml`, `VST/<slug>-arm32.so`, `VST/<slug>-arm64.so`, `Plugin Skins/` (touch UI, with `browser_images/soundsmode.png`) and `Presets/` (one `.xpl` per built-in program, written by the build). `tools/build.py` produces it from a project in `plugins/<Name>/` (`plugin.json`, `src/*.cpp`, `ui.json`, `README.md`, optional `Content/`, `thumbnail.png` (270x110 browser picture; a default is drawn if absent) and `presets.json` (folders for the patch browser)). Samples, wavetables, impulse responses and other files the plugin needs at run time go in the project's `Content/` folder, which is copied to `Content/` beside `Plugin Skins/`; the code finds it with `mpc::contentPath("...")` from `framework/mpc_resources.h` (see `docs/plugin-folder-format.md`). Users copy the folder into a `VST2 Plugins` folder on a drive. The MPC must run MPC OS 3.10.1 or later, and External plugins must be enabled on the device (Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support). Always tell the user both of these when handing over a plugin. For an **effect**, also tell them it runs on the MPC Sample as Knob FX (firmware v1.4.0 or later; MIDI Config → Dev Enhancements (Beta), then Custom Plug-In Support), installed through the MPC Sample's microSD card in SD Card Access mode (steps in `docs/knob-fx.md`; the `mpc-plugin-deploy` skill walks through them). For an **instrument**, tell them it does not run on the MPC Sample.

## Skills

Use the skills in `.claude/skills/` (Claude Code) or `.agents/skills/` (Codex); they are the same files:

| Skill | When |
|---|---|
| `mpc-plugin-new` | Start a plugin from a description (scaffold → code → UI → build → test) |
| `mpc-plugin-dsp` | Write or change the audio code, parameters, MIDI, state |
| `mpc-plugin-ui` | Pages, knobs, layout, custom skins |
| `mpc-plugin-build` | Compile for native + arm32 + arm64, fix build/validation errors |
| `mpc-plugin-test` | Run the host simulator, interpret the report, render WAVs |
| `mpc-plugin-package` | Zip with install instructions for sharing |
| `mpc-plugin-deploy` | Install/list/update/remove/export plugins on an MPC over the network (`tools/mpc_remote.py`), and set the MPC up for it; install effects on an MPC Sample through its microSD card (SD Card Access mode) |
| `mpc-plugin-presets` | Built-in programs |
| `mpc-plugin-troubleshoot` | Problems on the MPC or with the tools |

## The workflow, every time

0. **Manufacturer name**: if the user has not given their name or brand in their prompt, ask for it before doing anything else (it becomes the plugin manufacturer). In the same message, show the plugin name you propose and let them change it. Never fall back to a generic default such as "MPC Vibecoder".
1. `python tools/new_plugin.py "<Name>" --vendor "<Manufacturer>" --kind instrument|effect`
2. Write `src/<Class>.cpp` against `framework/mpc_plugin.h` using `framework/dsp/*.h`; write `ui.json`; write `README.md`.
3. `python tools/build.py plugins/<Name>` until it is clean.
4. `.sdk-tools/host_sim plugins/<Name>/build/native/<slug>.<dylib|so|dll> test --wav plugins/<Name>/build/test.wav` until no FAIL and the WARNs are understood.
5. Always ASK whether they want a test install on a standalone MPC over the network (ask for its IP address), and install only if they say yes. Tell the user where the output folder is and how to install it; offer `python tools/package.py plugins/<Name>`, and, if their MPC is on the same network, offer to install it for them with the `mpc-plugin-deploy` skill. For an effect and an MPC Sample, offer the same skill's SD card install instead (install only on a yes; it also asks before ejecting the card).

Never report a plugin as done without steps 3 and 4 passing.

Commands here say `python`; on macOS and some Linux systems only `python3` exists, so use that if `python` is not found.

## Hard rules

- **Realtime**: no allocation, locks, logging or I/O in `process()`. Load files from `Content/` in the constructor or `prepare()` only; never hard-code absolute paths. Block size is a maximum; frames per call vary (32–512). Clamp inputs to filters and divisions.
- **Identity is permanent**: `uid` (plugin.json and `MPC_FOURCC` in code must match), parameter `id` strings, and parameter order must not change once a user has projects. Append new parameters at the end.
- **Ask for the manufacturer name** when the prompt does not contain one, and show the proposed plugin name for confirmation. Do not invent a generic default.
- **Names must agree**: `plugin.json` `name`/`manufacturer` == `PluginInfo` `name`/`vendor` == folder name parts. The UI is found by those strings.
- **Effects get no MIDI; plugins cannot send MIDI to the MPC.** Sequencers and arpeggiators are instruments that make their own sound.
- **Every effect ships an MPC Sample Knob FX mapping**: `shortName`, `knobs` and `shiftKnobs` in `plugin.json`, at most 3 knobs each, each with a 0-based `parameter` index and a short `label`. On the MPC Sample those 6 parameters are the plugin's entire UI (no skins, no edit screen), and it loads no instruments. The mapping is as permanent as parameter order. See `docs/knob-fx.md`.
- **Parameter 0 is nudged by the host on every load.** Make it harmless (master volume) with no side effects.
- **CPU**: the MPC is a slow ARM device. Aim for a few percent of one core per instance; no per-sample transcendental functions in inner loops.
- **Nothing proprietary**: do not fetch, copy or depend on files from MPC firmware, Steinberg's VST SDK, or code with licences incompatible with the user's intent. Note the licence of any DSP you borrow in the project README.
- **User stays in control of installs.** The user decides what is installed, updated or removed, and where. Never run `mpc_remote.py add`/`update`/`import`/`remove`/`restart` unless the user has explicitly asked for that action on that MPC, in this conversation. A request such as "install it on my MPC" or "update it when you're done" covers exactly what it names; a yes to one install does not cover the next build or another MPC, unless they said so ("update it every time you rebuild"). Otherwise finish the build, then ASK whether they want it installed on an MPC and wait for the answer. Silence, a clean build or a passing test is not permission.
- **No system modification**: never suggest rooting, firmware patches or editing MPC system files. The official route is the `VST2 Plugins` folder and the preference switch. The one remote route is `tools/mpc_remote.py` (skill `mpc-plugin-deploy`), which uses the MPC's official developer mode and only ever writes inside a drive's `VST2 Plugins` folder. Do not run other commands on the device over SSH, and do not hand-write SSH/scp commands to copy plugins.

## Where to read

| Need | File |
|---|---|
| Mental model | `docs/overview.md` |
| Which MPCs are supported and how they differ (32-bit, 64-bit, MPC Sample) | `docs/supported-devices.md` |
| Folder, plugin.json, version.xml | `docs/plugin-folder-format.md` |
| What the MPC host does | `docs/host-contract.md` |
| MPC Sample: effects as Knob FX, the 3 + 3 knob mapping | `docs/knob-fx.md` |
| Writing DSP | `docs/dsp-cookbook.md`, `framework/mpc_plugin.h`, `framework/dsp/` |
| Samples, wavetables, any file in the plugin folder | `framework/mpc_resources.h`, `framework/dsp/wav.h`, cookbook §11b, `docs/plugin-folder-format.md` |
| Simple UI | `docs/ui-guide.md`, `schemas/ui.schema.json` |
| Knob / fader / switch graphics | `ui-kit/README.md`, `ui-kit/preview.png`, `tools/make_ui_kit.py` |
| Full UI format | `docs/ui-reference.md` |
| Toolchains and flags | `docs/building.md` |
| Testing and installing | `docs/install-and-test.md` |
| Presets | `docs/presets.md` |
| Problems | `docs/troubleshooting.md` |
| Legal, JUCE question, community | `docs/faq-and-legal.md` |
| Worked examples | `examples/SimpleSynth`, `examples/StereoDelay`, `examples/DrumSynth` |

## Style for generated plugins

- One `.cpp` per plugin unless it grows past ~800 lines; then split DSP into headers under `src/`.
- Comment the signal flow at the top of the file and each parameter's purpose. The user will read it.
- Short, unit-bearing parameter names and sensible defaults; a master volume parameter first; 3–8 built-in programs.
- A `ui.json` that uses the UI kit: `knob`, `fader` and `switch` widgets (heuristics in the `mpc-plugin-ui` skill), not pages of generic sliders: never use the plain `slider` widget or a bare parameter name in a themed UI (long choice lists use `knob` with `"overlay": "menu"`), and treat the build's "generic slider" WARNING as a bug to fix. Arrange controls in blocks of four, one related set each, all in the plugin's single `"qlinkGroups"` orientation (columns by default, or rows; never mixed), so the Q-Link rectangle encloses each set cleanly.
- Keep the project README honest: what works, what is not implemented, CPU expectations.
