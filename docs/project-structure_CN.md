# 项目结构

Language: [English](project-structure.md) | 简体中文

## 源码树

- `src/wrapper/ZygiskEntryWrapper.cpp`：Zygisk 入口，以及 `Runtime.nativeLoad` Hook。
- `src/wrapper/JniEntryWrapper.cpp`：JNI 入口（`JNI_OnLoad`），用于 `libcocos2dcpp.so` 加载后的注入路径。
- `src/wrapper/WrapperCommon.hpp`：包装层公共辅助（游戏库检测、包名校验、功能初始化）。
- `src/manager/GameManager.{hpp,cpp}`：缓存共享运行时状态（当前主要是 `libcocos2dcpp.so` 基址）。
- `src/manager/HookManager.{hpp,cpp}`：通用解析/Hook 管理器（`InstallInlineHook`、`CALL_ORIG`、Hook 恢复辅助）。
- `src/manager/NetworkManager.{hpp,cpp}`：管理网络 Hook、构建请求上下文，并按优先级分发已注册处理器。
- `src/manager/network/NetworkHandler.hpp`：统一处理器 API（`HandlerArgs`、`ActiveRequestCtx`、阶段、body 视图）。
- `src/features/Feature.hpp`：功能基类；接入通用管理器访问与缓存库基址。
- `src/features/Autoplay.{hpp,cpp}`：游玩逻辑 Hook、自动游玩流程和核心游戏行为补丁。
- `src/features/NetworkLogger.{hpp,cpp}`：高优先级网络审计处理器（请求/响应日志）。
- `src/features/NetworkBlock.{hpp,cpp}`：最低优先级网络拦截策略处理器。
- `src/GameTypes.hpp`：`Gameplay`、`LogicArcNote`、`LogicHoldNote` 等轻量封装（基于偏移）。
- `src/utils/MemoryUtils.hpp`：内存工具兼容汇总头。
- `src/utils/memory/*.hpp|*.cpp`：拆分后的内存工具（`ProcMaps`、`AddressResolver`、`Patcher`、`InlineHook`、可执行区间辅助）。
- `src/utils/Log.h`：日志宏。
- `src/config/AutoplayConfig.h`：自动游玩的 Hook/偏移/特征常量。
- `src/config/NetworkBlockConfig.h`：网络 Hook/偏移/特征常量及拦截规则。
- `src/config/ModuleConfig.h`：模块共享常量、目标包名列表和功能开关。
- 配置命名空间布局：`arc_autoplay::cfg::module`、`arc_autoplay::cfg::autoplay`、`arc_autoplay::cfg::network_block`。

## 初始化流程

1. 包装层检测到 `libcocos2dcpp.so` 就绪，并使用 `GameManager` 缓存的基址。
2. Zygisk 包装层在 `WrapperCommon` 中校验包名；JNI 包装层跳过包名校验。
3. 包装层调用所有 `FeatureName::Instance()`。
4. 每个 `Instance()` 先检查自身配置开关，再由功能代码通过 `HookManager` 安装 Hook（若库基址未就绪则懒重试）。
