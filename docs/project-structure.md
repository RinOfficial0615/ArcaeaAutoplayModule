# Project Structure

Language: English | [简体中文](project-structure_CN.md)

## Source Tree

- `src/wrapper/ZygiskEntryWrapper.cpp`: Zygisk wrapper entrypoint and `Runtime.nativeLoad` hook.
- `src/wrapper/JniEntryWrapper.cpp`: JNI wrapper entrypoint (`JNI_OnLoad`) for post-`libcocos2dcpp.so` loading.
- `src/wrapper/WrapperCommon.hpp`: shared wrapper helpers (game lib detection, package check, and feature init).
- `src/manager/GameManager.{hpp,cpp}`: caches shared runtime state (currently `libcocos2dcpp.so` base).
- `src/manager/GameVersionManager.{hpp,cpp}`: resolves the real game version and activates the matching runtime profile.
- `src/manager/HookManager.{hpp,cpp}`: shared resolve/hook helper manager (`InstallInlineHook`, `CALL_ORIG`, hook restore helpers).
- `src/manager/NetworkManager.{hpp,cpp}`: owns network hooks, builds request context, and dispatches registered handlers by priority.
- `src/manager/network/NetworkHandler.hpp`: shared handler API (`HandlerArgs`, `ActiveRequestCtx`, phases, body views).
- `src/features/Feature.hpp`: feature base class; wires common manager access and cached lib base.
- `src/features/Autoplay.{hpp,cpp}`: gameplay hooks, autoplay flow, and core game-behavior patches.
- `src/features/NetworkLogger.{hpp,cpp}`: high-priority network audit handler (request/response logging).
- `src/features/NetworkBlock.{hpp,cpp}`: lowest-priority network block policy handler.
- `src/GameTypes.hpp`: lightweight wrappers for `Gameplay`, `LogicArcNote`, `LogicHoldNote`, etc (offset-based).
- `src/utils/MemoryUtils.hpp`: compatibility umbrella include for memory utilities.
- `src/utils/memory/*.hpp|*.cpp`: split memory utilities (`ProcMaps`, `AddressResolver`, `Patcher`, `InlineHook`, exec-range helper).
- `src/utils/Log.h`: logging macros.
- `src/config/GameProfile.hpp`: per-version runtime profile table (`6.12.11c` / `6.13.2f`).
- `src/config/AutoplayConfig.h`: shared autoplay layouts, behavior knobs, and byte signatures.
- `src/config/NetworkBlockConfig.h`: shared network layouts, policies, and byte signatures.
- `src/config/ModuleConfig.h`: shared module constants, target package list, and feature toggles.
- Config namespace layout: `arc_autoplay::cfg::module`, `arc_autoplay::cfg::autoplay`, `arc_autoplay::cfg::network_block`.

## Initialization Flow

1. Wrapper detects `libcocos2dcpp.so` is ready and uses `GameManager` cached base.
2. Zygisk wrapper validates package in `WrapperCommon`; JNI wrapper skips package check.
3. `GameVersionManager` detects the candidate build and waits for the real `appVersion` string.
4. Once the version is confirmed, wrapper-level callback code instantiates the feature singletons.
5. Each feature then installs hooks through `HookManager` using the active runtime profile.
