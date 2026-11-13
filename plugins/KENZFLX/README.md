# KENZFLX

An insert effect plugin for Akai MPC standalone hardware, by KENZ.

KENZFLX combines drive, tone shaping, chorus, delay, reverb, bit-crushing and
tempo-sliced effects in one stereo insert. The Reverse control blends between
the normal signal, half-time and beat-slice processing, then reverse-style
playback at its upper range.

The effect is an experimental DSP implementation; listen for artifacts and
save projects before using it on a live set.

## Build

```
python tools/build.py plugins/KENZFLX
python tools/package.py plugins/KENZFLX
```

## Project layout

- `plugin.json` - manifest (name, manufacturer, uid). The `uid` must match the C++ `uniqueId`.
- `src/KENZFLX.cpp` - the plugin source (parameters and DSP).
- `ui.json` - which parameters appear on the MPC screen, in which order.
- `build/` - build output (the deployable `KENZ - VST - KENZFLX` folder lives here).
- `dist/` - zips produced by `package.py`.
