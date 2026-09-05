# Version Support

Language: English | [简体中文](version-support_CN.md)

## Supported builds

- `6.12.11c`
- `6.13.2f`
- `6.14.0c`
- `6.16.2c` (full profile; custom charts)
- `6.16.8c` (full profile; custom charts)
- `7.0.0c` (full profile; custom charts — note-family derived layout shifted +8, see `docs/offsets/7.0.0c-offsets.md`)
- `7.0.1c` (full profile; custom charts — retains the 7.0.x note-family layout, see `docs/offsets/7.0.1c-offsets.md`)
- `7.0.255c` (full profile; custom charts — retains the 7.0.x note-family layout, see `docs/offsets/7.0.255c-offsets.md`)

## Runtime detection

1. Wrapper waits until `libcocos2dcpp.so` is mapped.
2. `GameVersionManager` first reads each profile's `appVersion` string global.
3. If the global is not initialized yet, it resolves and hooks the exported `Java_low_moe_AppActivity_setAppVersion` symbol with `dlsym`.
4. The real version string received by the hook selects the matching profile; feature hooks are armed only after that confirmation.

Unknown builds stay in a "detected, not armed" state — the wrong offsets never get applied.

## Adding a version

1. Append a new `GameVersionId` variant to `src/game/GameProfile.hpp`.
2. Fill in the version string global offset. The `setAppVersion` hook is resolved by its exported ELF symbol and does not need a per-build offset.
3. Fill in autoplay / network / ssl_pins / custom_charts offsets and capabilities for that build. To locate function offsets in the new build from the previous version's addresses, use `scripts/port_match.py` (workflow documented in its module docstring); `scripts/port_match_7000c_spec.json`, `scripts/port_match_7001c_spec.json`, and `scripts/port_match_70255c_spec.json` record complete worked mappings.
4. If object layouts changed, specialize the corresponding struct template in `src/game/GameStructs.hpp`.
5. Update this file.

## Offset organisation

- **Object layouts** — `src/game/GameStructs.hpp` (version-templated, compile-time verified via `offsetof` + `static_assert`).
- **Shared constants & signatures** — `src/config/AutoplayConfig.h`, `src/config/NetworkBlockConfig.h`, and `src/config/CustomChartConfig.h`.
- **Function / RTTI / patch-site offsets** — `src/game/GameProfile.hpp` (one entry per supported version).
- **Documented sites** — `docs/offsets/6.12.11c-offsets.md`, `docs/offsets/6.16.2c-offsets.md`, `docs/offsets/6.16.8c-offsets.md`, `docs/offsets/7.0.0c-offsets.md`, `docs/offsets/7.0.1c-offsets.md`, and `docs/offsets/7.0.255c-offsets.md`.
