# Version Support

Language: English | [简体中文](version-support_CN.md)

## Supported builds

- `6.12.11c`
- `6.13.2f`
- `6.14.0c`

## Runtime detection

1. Wrapper waits until `libcocos2dcpp.so` is mapped.
2. `GameVersionManager` checks known `Java_low_moe_AppActivity_setAppVersion` offsets to find the candidate build.
3. It then reads or hooks the native `appVersion` setter / global and waits for the real version string.
4. Hooks are armed only after the version string is fully confirmed.

Unknown builds stay in a "detected, not armed" state — the wrong offsets never get applied.

## Adding a version

1. Append a new `GameVersionId` variant to `src/config/GameProfile.hpp`.
2. Fill in the version probe offsets (`set_app_version` + `app_version_string` global).
3. Fill in autoplay / network / ssl_pins offsets for that build.
4. If object layouts changed, specialize the corresponding struct template in `src/config/GameStructs.hpp`.
5. Update this file.

## Offset organisation

- **Object layouts** — `src/config/GameStructs.hpp` (version-templated, compile-time verified via `offsetof` + `static_assert`).
- **Shared constants & signatures** — `src/config/AutoplayConfig.h` / `src/config/NetworkBlockConfig.h`.
- **Function / RTTI / patch-site offsets** — `src/config/GameProfile.hpp` (one entry per supported version).
