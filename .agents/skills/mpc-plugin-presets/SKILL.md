---
name: mpc-plugin-presets
description: Add factory presets (built-in programs and the browsable patch list) to an MPC plugin, or explain MPC preset (.xpl) files. Use when the user asks for presets, patches, programs, an init patch or a sound bank for their plugin.
---

# Presets

Two layers; the build writes the second from the first.

1. **Built-in programs** declared in code. They appear in the MPC's preset selector and are stored by number in projects.
2. **`Presets/*.xpl`** in the plugin folder: the MPC's patch browser (grid of named presets, with folders). `tools/build.py` generates one file per built-in program with `tools/make_presets.py`. Nothing to write by hand.

Steps:

1. Get the parameter order: `.sdk-tools/host_sim build/native/<slug>.dylib dump` (or read the constructor's `addParam` order).
2. In the constructor, after all `addParam` calls, add one `addProgram("Name", { plain values in parameter order })` per preset. Values are plain (Hz, dB, 0/1 for bools, choice index for choices), not normalised. Make program 0 a sensible "Init".
3. Keep lists complete and in order; a comment per value helps future edits. 8–16 presets covering distinct uses is a good factory set.
4. Optional folders in the patch browser: add `presets.json` beside `plugin.json`, mapping program names to a folder name (`{ "Clean Sub": "Bass", "Acid Fire": "Acid" }`). Unlisted presets stay at the top level.
5. Rebuild (`python tools/build.py plugins/<Name>`): the log says `wrote N preset files`, and the output folder has `Presets/`. Verify with `dump` (it lists program names) and `test`.

On the MPC the plugin must be reinstalled and the MPC restarted before the browser shows the presets. Details, file naming and the XML layout: `docs/presets.md`.
