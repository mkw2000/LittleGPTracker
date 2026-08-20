# LittleGPTracker fork work

This branch is the durable home for the LittleGPTracker work that was previously split between a local commit and an uncommitted working tree.

## Branch identity

- Branch: `codex/littlegptracker-stability-reverb`
- Base: upstream `master` at `877cd7a`
- Existing local baseline retained: `f861237` (`Fix PSP stability and project persistence`)
- Personal fork: `mkw2000/LittleGPTracker`
- Source fork remote: `djdiskmachine/LittleGPTracker` (`djdiskmachine`)
- Original project remote: `Mdashdotdashn/LittleGPTracker` (`upstream`)

The existing baseline commit is kept intact. Follow-up work is grouped by behavior so each commit can be reviewed, reverted, or cherry-picked independently.

## Included work

### Realtime track effects

- Adds allocation-free per-track processing after instrument playback and before each channel mix bus.
- Adds tempo-synced stereo Echo (`ECHO`, `ETIM`, `EFBK`), chorus (`CHOR`), flanger (`FLNG`), and a compact room reverb (`RVRB`).
- Routes the commands from both phrases and tables without changing the project file format.
- Keeps all effects disabled until commanded, preserving the sound of existing projects.

### PSP lifecycle and diagnostics

- Moves callback registration until after application and audio initialization.
- Adds a suspend/resume state machine with semaphores so the main thread tears down and restores SDL audio safely.
- Validates the negotiated SDL audio format and fences audio callbacks during shutdown.
- Rotates the previous `lgpt.log` to `lgpt-prev.log` at boot, caps each file at 256 KiB, and records detailed save/import breadcrumbs.
- Keeps the PSP executable in user mode; PSPSDK's kernel exception handler is intentionally not linked because real firmware rejects its `ForKernel` imports.

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

### PSP performance

- Uses PSP directory metadata directly and caches path types, avoiding repeated Memory Stick `stat` calls while browsing.
- Uses stable O(n log n) list sorting and avoids duplicating every visible browser path.
- Reads WAV previews ahead in bounded 4096-frame blocks instead of reading on nearly every audio slice.
- Reuses mixer iterators and scratch buffers and fast-paths the default fixed-point output path.
- Disables per-button event logging in release PSP builds, including when an older external config enables it.

## Validation

Realtime FX checks run on macOS on 2026-08-19:

- C++03 impulse tests passed for default-off bit-exact bypass, tempo-step Echo timing, chorus, flanger, reverb tails, and reset behavior.
- `make PLATFORM=MACOS -j4` produced and signed the arm64 app bundle.
- The built app completed a local startup smoke test.
- A fresh `make PLATFORM=PSP -j4` build passed with PSPDEV v20260801 and produced `projects/buildPSP/EBOOT.PBP`; real-device audio-load testing remains pending.

The following checks were run on macOS on 2026-08-11:

- `git diff --check` — passed.
- C++03 syntax check for `InstrumentView.cpp`, `FxPrinter.cpp`, and `NativeReverb.cpp` with the vendored SDL include path — passed with legacy TinyXML, enum, and `sprintf` warnings.
- `make` from `projects` — blocked by the checkout's existing generated dependency files (`Logger.d: missing target pattern`).
- Fresh `make BUILD=buildRASPICodex` — compiled the new crash-context/logging sources, then stopped because this Mac does not have the RASPI build's SDL1 header (`SDL/SDL.h`).

The PSP target builds with PSPDEV/PSPSDK. Run this from `projects` on a PSP build host:

```sh
make PLATFORM=PSP
```

Before treating a release as verified, test boot and suspend/resume on real hardware, WAV import of an existing project sample, and all four native reverb presets.

## Maintenance workflow

```sh
git switch codex/littlegptracker-stability-reverb
git pull --ff-only
git status --short
```

Keep new fixes in focused commits, update `CHANGELOG`, and add the relevant validation command here when platform-specific verification changes.
