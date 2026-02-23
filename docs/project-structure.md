# Project Structure

Language: English | [简体中文](project-structure_CN.md)

## Source Tree

- `src/wrapper/ZygiskEntryWrapper.cpp`: Zygisk wrapper entrypoint and `Runtime.nativeLoad` hook.
- `src/wrapper/JniEntryWrapper.cpp`: JNI wrapper entrypoint (`JNI_OnLoad`) for post-`libcocos2dcpp.so` loading.
- `src/wrapper/WrapperCommon.hpp`: shared wrapper helpers (game lib detection, package check, and feature init).
- `src/manager/GameManager.{hpp,cpp}`: caches shared runtime state (currently `libcocos2dcpp.so` base).
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
- `src/config/AutoplayConfig.h`: autoplay hook/offset/signature constants.
- `src/config/NetworkBlockConfig.h`: network hook/offset/signature constants and block rules.
- `src/config/ModuleConfig.h`: shared module constants, target package list, and feature toggles.
- Config namespace layout: `arc_autoplay::cfg::module`, `arc_autoplay::cfg::autoplay`, `arc_autoplay::cfg::network_block`.

## Initialization Flow

1. Wrapper detects `libcocos2dcpp.so` is ready and uses `GameManager` cached base.
2. Zygisk wrapper validates package in `WrapperCommon`; JNI wrapper skips package check.
3. Wrapper calls all `FeatureName::Instance()`.
4. Each `Instance()` gates on its own config switch, then feature code installs hooks via `HookManager` (with lazy re-try if lib base was not ready yet).
