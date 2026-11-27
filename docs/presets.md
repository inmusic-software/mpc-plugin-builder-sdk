# Presets

There are two layers, and the build does both for you.

1. **Built-in programs** declared in code with `addProgram(...)`. The MPC remembers the chosen program number in the project.
2. **Preset files** (`Presets/*.xpl`) in the plugin folder, which fill the MPC's patch browser (the grid of named presets, with folders, shown when you open the instrument in the Sounds browser). `tools/build.py` writes one file per built-in program, so the two always agree.

## Built-in programs

Declare presets in code with `addProgram("Name", { ...plain values in parameter order... })`. Your parameters snap to the stored values when one is chosen. This works on every product and cannot get out of sync with the plugin.

```cpp
// After addParam(...) calls, in the constructor:
addProgram("Init",      { 0.5f, 1000.0f, 0.2f, 0 });
addProgram("Deep Bass", { 0.9f,  220.0f, 0.6f, 1 });
```

Order matters: values are matched to parameters by index. Changing parameters later means updating the lists; the framework ignores extra values and leaves missing ones alone.

## The patch browser (Presets/*.xpl)

`tools/build.py` runs `tools/make_presets.py` after the skins step. It reads the state chunk of every program from `build/params.json` (written by `host_sim dump`, key `programChunks`) and writes, inside the output folder:

```
<Manufacturer> - VST - <Name>/Presets/000-Clean Sub.xpl
                                      001-Bass-Clean Sub.xpl        <- shown in a folder called "Bass"
                                      002-Bass-Dune Warm.xpl
                                      003-Acid-Acid Squelch.xpl     <- shown in a folder called "Acid"
```

The file name is `NNN-Name.xpl` or `NNN-Category-Name.xpl`: `NNN` is the preset index, `Category` the folder shown in the browser, `Name` the preset. Names are cleaned for use in file names (`-` and path characters become spaces); the real name is kept inside the file.

### Choosing folders: presets.json

By default every preset is at the top level of the browser. To group them, add `presets.json` next to `plugin.json`, mapping program names to folder names:

```json
{
  "Clean Sub": "Bass",
  "Dune Warm": "Bass",
  "Acid Squelch": "Acid"
}
```

When folders are used, `make_presets.py` groups presets contiguously by category and also places `000-<Program0>.xpl` at the top level so that selecting the instrument in the MPC's Sounds browser opens the top-level folder view rather than jumping straight into the first subfolder. Names that are not listed stay at the top level. Keep the file in step with your `addProgram` names.

### File layout

```xml
<?xml version="1.0" encoding="UTF-8"?>

<pluginstate>
  <version file="1" date="2026-11-17-10-24"/>
  <PLUGIN name="Wobble" format="VST" category="Synth" manufacturer="Acme" version="1.0.0"
          file="VST/wobble-arm64.so" uid="1094796898" isInstrument="1"
          fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>
  <preset>Deep Bass</preset>
  <state64>BASE64-ENCODED-STATE-CHUNK</state64>
</pluginstate>
```

- `uid` is the decimal form of the plugin's four-character id (`params.json` has it as `uidInt`).
- `state64` is the plugin's state chunk (the same bytes the project stores), base64 encoded. For SDK plugins that chunk is the framework format: a header, each parameter's id and plain and normalised value, then any extra state.

### Other places presets live

Presets that a user saves from the plugin's edit screen are stored under the user's `Plugin Presets` folder, in the same `.xpl` format. Those are per user and per drive; the factory presets above travel with the plugin folder.

> [!NOTE]
> The file layout follows how the MPC's own instruments are laid out. Check on your own MPC (install, restart, open the instrument in the Sounds browser); if firmware changes the layout, `make_presets.py` is the one place to adapt.
