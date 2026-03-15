# ArcAutoplayModule

Language: [English](README.md) | 简体中文

Arcaea 自动打歌的模块（6.12.11c / 6.13.2f，arm64）

本模块为 Zygisk 和 JNI 分别实现了两套 wrapper，这意味着你既可以直接把编译出来的 so 塞到 apk 里，然后改 dex 让它在 `libcocos2dcpp.so` 之后加载，也可以直接安装 Zygisk 模块。

## 环境要求

- Android NDK r28+（构建脚本会自动选择已安装的最新版本；若最新版本 <= r28 会报错）
- 已启用 Zygisk 的设备
- Arcaea 6.12.11c 或 6.13.2f

## 构建

本仓库使用了子模块：

```powershell
git submodule update --init --recursive
```

1. 设置 `ANDROID_NDK_HOME`
2. 构建 release：

```powershell
./build.ps1 --rel
```

构建产物：`build/ArcAutoplayModule.zip`

## 说明

- 本项目包含大量 Vibe Coding 产物，AI 味可能比较重。

---

- 模块默认目标包名为 `moe.inf.arc`、`moe.low.arc`（若你使用改包名版本，也支持 `moe.low.mes`）。
- JNI 入口：若通过 JNI 加载，会在 `libcocos2dcpp.so` 已加载后初始化，并且不校验包名。
- 功能开关位于 `src/config/ModuleConfig.h` 的 `arc_autoplay::cfg::module` 下：`kAutoplayEnabled`、`kNetworkLoggerEnabled`、`kNetworkBlockEnabled`。
- 配置分为共享功能常量（`cfg::module`、`cfg::autoplay`、`cfg::network_block`）以及 `src/config/GameProfile.hpp` 中的版本 profile。
- 运行时共享管理器位于 `src/manager`：`GameManager`（缓存游戏库基址）、`HookManager`（解析/Hook 辅助、原函数调用、Hook 恢复）、`NetworkManager`（网络 Hook + 处理器分发）。
- 网络功能采用处理器链：`NetworkLogger`（高优先级审计）后接 `NetworkBlock`（最低优先级策略）。
- 当前网络拦截规则在 `src/config/NetworkBlockConfig.h` 的 `kBlockRules` 中按规则配置。
- 支持版本会在运行时由 `GameVersionManager` 通过原生 `appVersion` 字符串动态确认。
- 若你使用其他游戏版本，需要在 `src/config/GameProfile.hpp` 中新增 profile，并同步更新有变化的共享签名或结构布局常量。
- 偏移参考文档：`docs/6.12.11c-offsets.md` 与 `docs/version-support.md`。

## 文档

- 项目结构：`docs/project-structure.md`
- 版本支持：`docs/version-support.md`

## TODO

1. 加一个 GUI 界面，动态切换功能开关
2. 利用 `NetworkManager`，实现一些其他功能，比如改服务器 endpoint
