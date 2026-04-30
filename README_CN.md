# ArcAutoplayModule

Language: [English](README.md) | 简体中文

Arcaea 自动打歌模块。支持 6.12.11c / 6.13.2f / 6.14.0c（arm64）。

提供 Zygisk 和 JNI 两种注入方式：你可以把编译出的 so 塞进 apk 改 dex 加载，也可以直接装 Zygisk 模块。

## 环境

- Android NDK r28+（构建脚本自动选最新版；低于 r28 会报错）
- 已启用 Zygisk 的设备
- Arcaea 6.12.11c、6.13.2f 或 6.14.0c

## 构建

仓库用了子模块，先拉一下：

```powershell
git submodule update --init --recursive
```

然后设好 `ANDROID_NDK_HOME`，构建 release：

```powershell
./build.ps1 --rel
```

产物：`build/ArcAutoplayModule.zip`

## 功能

| 功能 | 开关 (ModuleConfig.h) | 说明 |
|------|----------------------|------|
| 自动打歌 | `kAutoplayEnabled` | 接管蛇和长条 touch，强制 Pure，精简特效 |
| 网络日志 | `kNetworkLoggerEnabled` | 审计全部 HTTP 请求/响应 |
| 网络拦截 | `kNetworkBlockEnabled` | 屏蔽上传分数/世界模式等请求 |
| SSL 反抓包 | `kDisableSslPinsEnabled` | 去掉 SSL pinning，默认关闭 |

## 运行时版本识别

模块加载后不会立刻装 hook。`GameVersionManager` 先探测游戏版本，等原生 `appVersion` 确认匹配后才会激活。不支持的版本会停在"已检测"状态，不会误用错误偏移。

## 配置位置

- `src/config/GameProfile.hpp` — 各版本的函数/RTTI/patch 偏移
- `src/config/GameStructs.hpp` — 游戏对象布局（含显式 padding，编译期校验）
- `src/config/AutoplayConfig.h` — 自动打歌的行为常量和字节签名
- `src/config/NetworkBlockConfig.h` — 网络策略、拦截规则、字节签名
- `src/config/ModuleConfig.h` — 功能开关、目标包名

## AI

本项目由 AI 辅助开发。

## 文档

- 项目结构：`docs/project-structure.md`
- 版本支持：`docs/version-support.md`
- 偏移参考：`docs/6.12.11c-offsets.md`
