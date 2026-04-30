# ArcAutoplayModule

Language: English | [简体中文](README_CN.md)

Arcaea autoplay module for 6.12.11c / 6.13.2f / 6.14.0c (arm64).

Provides both Zygisk and JNI entry points. You can embed the built .so into an APK and load it after `libcocos2dcpp.so`, or install it as a Zygisk module directly.

## Requirements

- Android NDK r28+ (the build script picks the newest; fails if ≤ r28)
- Device with Zygisk enabled
- Arcaea 6.12.11c, 6.13.2f, or 6.14.0c

## Build

This repo uses a submodule — fetch it first:

```powershell
git submodule update --init --recursive
```

Set `ANDROID_NDK_HOME`, then build:

```powershell
./build.ps1 --rel
```

Artifact: `build/ArcAutoplayModule.zip`

## Features

| Feature | Toggle (ModuleConfig.h) | Purpose |
|---------|------------------------|---------|
| Autoplay | `kAutoplayEnabled` | Drive arcs & holds, force Pure, suppress effects |
| Network logging | `kNetworkLoggerEnabled` | Audit HTTP request / response traffic |
| Network block | `kNetworkBlockEnabled` | Block score uploads, world-mode calls, etc |
| SSL pinning bypass | `kDisableSslPinsEnabled` | Remove SSL pinning (off by default) |

## Runtime version detection

Hooks are not installed immediately. `GameVersionManager` probes the game build and waits until the native `appVersion` string matches a supported version. Unknown builds stay in a "detected, not armed" state — wrong offsets are never applied.

## Configuration layout

- `src/config/GameProfile.hpp` — per-version function / RTTI / patch offsets
- `src/config/GameStructs.hpp` — game object layouts (padded, compile-time verified)
- `src/config/AutoplayConfig.h` — autoplay behaviour knobs & byte signatures
- `src/config/NetworkBlockConfig.h` — network policy, block rules, byte signatures
- `src/config/ModuleConfig.h` — feature toggles, target package names

## AI

This project was developed with AI assistance.

## Docs

- Project structure: `docs/project-structure.md`
- Version support: `docs/version-support.md`
- Offsets reference: `docs/6.12.11c-offsets.md`
