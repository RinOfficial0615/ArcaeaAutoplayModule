# 版本支持

Language: [English](version-support.md) | 简体中文

## 当前支持的版本

- `6.12.11c`
- `6.13.2f`

## 运行时如何识别版本

1. wrapper 先等待 `libcocos2dcpp.so` 完成加载。
2. `GameVersionManager` 先用已知的 `Java_low_moe_AppActivity_setAppVersion` 偏移判断候选版本。
3. 然后读取或 Hook 原生侧的 `appVersion` setter / 全局字符串，等待游戏自己上报真实版本号。
4. 只有当版本字符串与支持列表完全匹配时，模块才会继续安装 autoplay / network hooks。

这样做可以让不支持的版本停留在“已检测但未启用”的安全状态，而不是误用错误偏移。

## 配置表位置

- 各版本的 hook 偏移集中在 `src/config/GameProfile.hpp`。
- 共享的签名、结构布局常量和行为配置仍放在 `src/config/AutoplayConfig.h` 与 `src/config/NetworkBlockConfig.h`。

## 说明

- JNI 和 Zygisk 两条注入路径共用同一套版本识别逻辑。
- 如果 `appVersion` 还没有解析出来，功能安装会先延后，并在版本 setter 被调用后自动重试。
