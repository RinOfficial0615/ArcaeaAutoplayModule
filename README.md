# ArcAutoplayModule

Language: English | [简体中文](README_CN.md)

A module that implements Arcaea autoplay (6.12.11c / 6.13.2f, arm64).

This module provides separate wrappers for Zygisk and JNI, which means you can either package the built so directly into an apk and modify dex so it loads after `libcocos2dcpp.so`, or just install it as a Zygisk module.

## Requirements

- Android NDK r28+ (build script auto-selects newest installed; errors if newest is <= r28)
- Device with Zygisk enabled
- Arcaea 6.12.11c or 6.13.2f

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

- This project contains many vibe-coding artifacts.

---

- This module targets package names `moe.inf.arc`, `moe.low.arc` (and `moe.low.mes` if you use a renamed build).
- JNI wrapper entry: if loaded via JNI, it initializes after `libcocos2dcpp.so` is already loaded and does not check package name.
- Feature switches are in `src/config/ModuleConfig.h` under `arc_autoplay::cfg::module`: `kAutoplayEnabled`, `kNetworkLoggerEnabled`, `kNetworkBlockEnabled`.
- Config is split into shared feature constants (`cfg::module`, `cfg::autoplay`, `cfg::network_block`) plus version profiles in `src/config/GameProfile.hpp`.
- Runtime shared managers are in `src/manager`: `GameManager` (cached lib base), `HookManager` (resolve/hook helpers, call-original, restore), and `NetworkManager` (network hooks + handler dispatch).
- Network features are handler-based: `NetworkLogger` (higher priority audit) then `NetworkBlock` (lowest priority policy).
- Current network block list is rule-based in `src/config/NetworkBlockConfig.h` (`kBlockRules`).
- Supported builds are selected at runtime by `GameVersionManager` using the native `appVersion` string.
- If you use a different game version, add a new profile in `src/config/GameProfile.hpp` and update any changed shared signatures/layout constants.
- Offsets reference: `docs/6.12.11c-offsets.md` and `docs/version-support.md`.

## Docs

- Project structure: `docs/project-structure.md`
- Version support: `docs/version-support.md`

## TODO

1. Add a GUI panel to dynamically toggle feature switches.
2. Build more features on top of `NetworkManager`, for example custom server endpoint redirection.
