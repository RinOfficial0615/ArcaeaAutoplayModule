# ArcHelperModule

Language: [English](README.md) | 简体中文

Arcaea 辅助模块，提供自动打歌、网络控制与本地自定义谱面加载（arm64）。

提供 Zygisk 和 JNI 两种注入方式：你可以把编译出的 so 塞进 apk 改 dex 加载，也可以直接装 Zygisk 模块。

## 环境

- Android NDK r30+（构建脚本自动选最新版；r29 及以下会报错）
- 仅 Zygisk 模式：设备已启用 Zygisk
- Arcaea 6.12.11c、6.13.2f、6.14.0c、6.16.2c、6.16.8c、7.0.0c 或 7.0.1c

## 构建

仓库用了子模块，先拉一下：

```powershell
git submodule update --init --recursive
```

然后设好 `ANDROID_NDK_HOME`，构建 release：

```powershell
./build.ps1 --rel
```

产物：`build/ArcHelperModule.zip`

`module/scope.txt` 会一并打包到模块根目录。每行填写一个精确包名，空行和 `#` 注释会忽略；文件存在时内容为唯一作用域，删空即停用全部目标包。文件缺失时回退到内置的三个包名。

## 功能

| 功能 | `config.json` 键 | 说明 |
|------|-------------------|------|
| 日志 | `Logging.level` | 写入 logcat 和文件的最低级别（`Debug`、`Info`、`Warn`、`Error`）；Debug 构建默认为 `Debug`，Release 构建默认为 `Info` |
| 自动打歌 | `Autoplay` | 接管蛇和长条 touch，强制 Pure，精简特效 |
| 网络日志 | `NetworkLogger` | 审计全部 HTTP 请求/响应，默认关闭 |
| 常规网络拦截 | `NetworkBlock` | 屏蔽上传分数/世界模式等规则；自定义谱面的固定隔离规则独立生效 |
| SSL 反抓包 | `SslPinningBypass` | 去掉有完整 patch profile 的版本上的 SSL pinning，默认关闭 |
| 自定义谱面 | `CustomCharts` | 启动时加载 `.arcpkg` 与 raw ZIP，并启用固定网络隔离规则 |

## 运行时版本识别

模块加载后不会立刻装 hook。`GameVersionManager` 先探测游戏版本，等原生 `appVersion` 确认匹配后才会激活。不支持的版本会停在"已检测"状态，不会误用错误偏移。

## 配置位置

运行时目录：`Android/data/<包名>/files/ArcHelper/`。各 Feature 实例会生成并规范化 `config.json`，自定义谱面从 `charts/` 扫描，进程日志写入 `logs/`，总计保留五份。

配置和谱面目录每个进程只读取一次。已注册字段类型或范围错误时会写回默认值，未知字段保留；JSON 整体损坏时重新生成全部 Feature 默认值。raw ZIP 最低只需音频和 AFF，自定义难度槽位支持 `0..4`。音频必须已经是 OGG Vorbis（`base.ogg`）；WAV 和其他编码会按原样落下，导入时不会转码。

- `src/game/GameProfile.hpp` — 各版本的函数/RTTI/patch 偏移
- `src/game/GameStructs.hpp` — 游戏对象布局（含显式 padding，编译期校验）
- `src/config/AutoplayConfig.h` — 自动打歌的行为常量和字节签名
- `src/config/NetworkBlockConfig.h` — 网络策略、拦截规则、字节签名
- `module/config.example.json` — 运行时配置示例
- `module/scope.txt` — 打包到 ZIP 根目录的模块作用域

第三方源码固定在 `third_party/json`、`third_party/libcxx`、`third_party/lsplt`、`third_party/magic_enum`、`third_party/rapidyaml` 和 `third_party/stb`。libcxx 子模块固定为 `d5117df3ba7704aab06c3a30b97c7529c931662b`，并作为模块的静态 C++ 运行库链接；`magic_enum` 固定为 v0.9.8。Zygisk API 头文件位于 `third_party/zygisk.hpp`。

## AI

本项目由 AI 辅助开发。

## 文档

- 项目结构：`docs/project-structure.md`
- 版本支持：`docs/version-support.md`
- 现代 C++ 约定：`docs/cpp/index.md` —— 工具链、实测矩阵、构建陷阱、用法约定
- 偏移参考：`docs/offsets/6.12.11c-offsets.md`、`docs/offsets/6.16.2c-offsets.md`、`docs/offsets/6.16.8c-offsets.md`、`docs/offsets/7.0.0c-offsets_CN.md`、`docs/offsets/7.0.1c-offsets_CN.md`
