# ArcHelperModule

Language: English | [简体中文](README_CN.md)

Arcaea helper module with autoplay, network controls, and local custom charts (arm64).

Provides both Zygisk and JNI entry points. You can embed the built .so into an APK and load it after `libcocos2dcpp.so`, or install it as a Zygisk module directly.

## Requirements

- Android NDK r30+ (the build script picks the newest; fails if ≤ r29)
- Zygisk mode only: device with Zygisk enabled
- Arcaea 6.12.11c, 6.13.2f, 6.14.0c, 6.16.2c, 6.16.8c, 7.0.0c, 7.0.1c, or 7.0.255c

## Build

This repo uses a submodule — fetch it first:

```powershell
git submodule update --init --recursive
```

Set `ANDROID_NDK_HOME`, then build:

```powershell
./build.ps1 --rel
```

Artifact: `build/ArcHelperModule.zip`

## Features

| Feature | `config.json` key | Purpose |
|---------|-------------------|---------|
| Logging | `Logging.level` | Minimum level written to logcat and files (`Debug`, `Info`, `Warn`, `Error`); defaults to `Debug` in debug builds and `Info` in release builds |
| Autoplay | `Autoplay` | Drive arcs & holds, force Pure, suppress effects |
| Network logging | `NetworkLogger` | Audit HTTP request / response traffic (off by default) |
| Ordinary network block | `NetworkBlock` | Apply score/world-mode URL rules; mandatory custom-chart isolation remains independent. `blocked_log_limit` caps logged blocked-request bodies; `0` means unlimited |
| SSL pinning bypass | `SslPinningBypass` | Remove SSL pinning on profiles with complete patch offsets (off by default) |
| Custom charts | `CustomCharts` | Load `.arcpkg` and raw ZIP at startup with fixed network-isolation rules |

`CxaThrowTracer` is an always-on diagnostic with no configuration key: it logs every C++ exception thrown through the game library (type name, caller, short unwind) to help diagnose crashes.

## Runtime version detection

Hooks are not installed immediately. `GameVersionManager` probes the game build and waits until the native `appVersion` string matches a supported version. Unknown builds stay in a "detected, not armed" state — wrong offsets are never applied.

## Configuration layout

Runtime directory: `Android/data/<package>/files/ArcHelper/`. Feature instances generate and normalize `config.json`, custom charts are scanned from `charts/`, and process logs are written under `logs/` with five files retained.

Configuration and packages are read once per process. Invalid registered values are rewritten to defaults while unknown keys are preserved; malformed JSON regenerates all Feature defaults. A raw ZIP needs only audio plus AFF files, and custom difficulty slots `0..4` are supported. Audio must already be OGG Vorbis (`base.ogg`); WAV and other encodings are stored as-is and are not transcoded.

- `src/game/GameProfile.hpp` — per-version function / RTTI / patch offsets
- `src/game/GameStructs.hpp` — game object layouts (padded, compile-time verified)
- `src/config/AutoplayConfig.h` — autoplay behaviour knobs & byte signatures
- `src/config/NetworkBlockConfig.h` — network policy, block rules, byte signatures
- `src/config/ModuleConfig.h` — module identity and target library names
- `module/config.example.json` — example runtime configuration
- `module/scope.txt` — module scope packaged at the ZIP root

Third-party sources are pinned at `third_party/json`, `third_party/libcxx`, `third_party/lsplt`, `third_party/magic_enum`, `third_party/rapidyaml`, and `third_party/stb`. The libcxx submodule is fixed at `d5117df3ba7704aab06c3a30b97c7529c931662b` and linked as the module's static C++ runtime. `magic_enum` is pinned to v0.9.8. The Zygisk API header is stored at `third_party/zygisk.hpp`.

## AI

This project was developed with AI assistance.

## Docs

- Project structure: `docs/project-structure.md`
- Version support: `docs/version-support.md`
- Modern C++ notes: `docs/cpp/index.md` — toolchain, feature matrix, build pitfalls, usage conventions
- Offsets reference: `docs/offsets/6.12.11c-offsets.md`, `docs/offsets/6.16.2c-offsets.md`, `docs/offsets/6.16.8c-offsets.md`, `docs/offsets/7.0.0c-offsets.md`, `docs/offsets/7.0.1c-offsets.md`, `docs/offsets/7.0.255c-offsets.md`
