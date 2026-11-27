# Contributing

Thanks for helping. A few ground rules keep the SDK safe to redistribute:

- **No Steinberg VST SDK code**, ever, in any form. The ABI header is a clean-room description; keep it that way.
- **No files extracted from MPC firmware** (skins, images, fonts, binaries, settings files). Describe formats; do not copy artefacts.
- **No MPC system modifications.** The SDK targets the official plugin folder and preference; pull requests that depend on root access, patched binaries or custom firmware will be declined.
- **MIT only** for anything added to `framework/`, `tools/`, `templates/` and `docs/`. Examples may use other OSI licences if clearly marked, but prefer MIT so people can reuse them freely.
- **Keep it simple for an AI agent.** Docs are read by coding agents; be exact about names, versions and paths. Tools are stdlib Python with friendly error messages.

Workflow: fork, branch, run `python tools/build.py examples/SimpleSynth` (all three examples should build and validate), open a pull request describing what changed and on which MPC you tested.

Versioning: releases are marked with annotated git tags (`v1.0`, `v1.1`, ...). Add a line for every user-visible change (new feature, changed behaviour, fix) under **Unreleased** in [docs/changelog.md](docs/changelog.md) as part of the same pull request. The SDK version is in the [VERSION](VERSION) file at the root (`python tools/build.py --version` prints it, and every build logs it). When a release is cut, the changelog heading is renamed to the version number, `VERSION` is set to that number (for example `1.2`), that commit is tagged `v1.2`, and then `VERSION` moves on to the next development version (`1.3-dev`).

Reporting problems: open an issue with your MPC model, firmware version, the output of `python tools/validate.py` and the host simulator report.
