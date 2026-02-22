# ArcAutoplayModule

Zygisk module that implements Arcaea autoplay (6.12.11c, arm64).

## Requirements

- Android NDK r28+ (build script auto-selects newest installed; errors if newest is <= r28)
- Device with Zygisk enabled
- Arcaea 6.12.11c (these offsets are version-specific)

## Build

This repo uses a submodule:

```powershell
git submodule update --init --recursive
```

1. Set `ANDROID_NDK_HOME`
2. Build release:

```powershell
./build.ps1 --rel
```

Build artifact: `build/ArcAutoplayModule.zip`

## Notes

- This module targets package names `moe.inf.arc`, `moe.low.arc` (and `moe.low.mes` if you use a renamed build).
- If you use a different game version, update offsets/signatures in `AutoplayConfig.h`.
- Offsets reference: `docs/6.12.11c-offsets.md`.

## Code Layout

- `ArcAutoplayModule.cpp`: Zygisk/JNI entrypoint + `Runtime.nativeLoad` hook.
- `AutoplayEngine.{h,cpp}`: gameplay hooks, autoplay flow, and core game-behavior patches.
- `GameTypes.h`: lightweight wrappers for `Gameplay`, `LogicArcNote`, `LogicHoldNote`, etc (offset-based).
- `MemoryUtils.{h,cpp}`: `/proc/self/maps` parsing, signature resolving, and ARM64 patch/hook utilities.
- `AutoplayConfig.h`: version-specific offsets and instruction signatures.

All source/header files live under `src/`.
