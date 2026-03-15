# Version Support

Language: English | [简体中文](version-support_CN.md)

## Supported Builds

- `6.12.11c`
- `6.13.2f`

## How Runtime Detection Works

1. The wrapper waits until `libcocos2dcpp.so` is mapped.
2. `GameVersionManager` checks known `Java_low_moe_AppActivity_setAppVersion` offsets to find the matching build candidate.
3. It then reads or hooks the native `appVersion` setter/global string and waits for the real version string from the game.
4. Only after the version string exactly matches a supported build does the module install autoplay/network hooks.

This keeps unsupported builds in a safe "detected but not armed" state instead of applying the wrong offsets.

## Profile Table

- Version-specific hook offsets live in `src/config/GameProfile.hpp`.
- Shared signatures, layout constants, and behavior knobs stay in `src/config/AutoplayConfig.h` and `src/config/NetworkBlockConfig.h`.

## Notes

- JNI and Zygisk wrappers share the same runtime detection path.
- If `appVersion` is not resolved yet, feature installation is deferred and retried from the version setter hook.
