# LittleGPTracker fork work

This branch is the durable home for the LittleGPTracker work that was previously split between a local commit and an uncommitted working tree.

## Branch identity

- Branch: `codex/littlegptracker-stability-reverb`
- Base: upstream `master` at `877cd7a`
- Existing local baseline retained: `f861237` (`Fix PSP stability and project persistence`)
- Intended personal fork: `mkw2000/LittleGPTracker`

The existing baseline commit is kept intact. Follow-up work is grouped by behavior so each commit can be reviewed, reverted, or cherry-picked independently.

## Included work

### PSP lifecycle and diagnostics

- Moves callback registration until after application and audio initialization.
- Adds a suspend/resume state machine with semaphores so the main thread tears down and restores SDL audio safely.
- Validates the negotiated SDL audio format and fences audio callbacks during shutdown.
- Adds `lgpt-crash.log` with registers, stack frames, free-memory values, and the last trace breadcrumb.
- Rotates the previous `lgpt.log` to `lgpt-prev.log` at boot.

### Native reverb rendering

- Replaces the external FFmpeg PrintFX path with a small native stereo reverb renderer.
- Keeps preset selection, wet amount, and tail length non-destructive.
- Adds an explicit `Render reverb` action in Instrument View.
- Preserves the source WAV and chooses a numbered output name when needed.
- Validates the rendered sample before assigning it to the instrument.

### Import, persistence, and UI safety

- Reuses an already-imported WAV instead of copying a duplicate into the project.
- Stops preview I/O before import, guards empty directory selections, and releases directory handles.
- Adds save breadcrumbs and safer per-line file logging.
- Makes mixed variable/action fields safe to inspect through the base `UIField` interface.
- Corrects the shared `MAX` helper, which previously clamped positive values to zero.

## Validation

The following checks were run on macOS on 2026-08-11:

- `git diff --check` — passed.
- C++03 syntax check for `InstrumentView.cpp`, `FxPrinter.cpp`, and `NativeReverb.cpp` with the vendored SDL include path — passed with legacy TinyXML, enum, and `sprintf` warnings.
- `make` from `projects` — blocked by the checkout's existing generated dependency files (`Logger.d: missing target pattern`).
- Fresh `make BUILD=buildRASPICodex` — compiled the new crash-context/logging sources, then stopped because this Mac does not have the RASPI build's SDL1 header (`SDL/SDL.h`).

The PSP target still needs a PSPDEV/PSPSDK toolchain and hardware or PPSSPP testing. Run this from `projects` on a PSP build host:

```sh
make PLATFORM=PSP
```

Before treating a release as verified, test suspend/resume, a forced crash report, WAV import of an existing project sample, and all four native reverb presets.

## Maintenance workflow

```sh
git switch codex/littlegptracker-stability-reverb
git pull --ff-only
git status --short
```

Keep new fixes in focused commits, update `CHANGELOG`, and add the relevant validation command here when platform-specific verification changes.
