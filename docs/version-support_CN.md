# 版本支持

Language: [English](version-support.md) | 简体中文

## 当前支持版本

- `6.12.11c`
- `6.13.2f`
- `6.14.0c`

## 运行时识别

1. Wrapper 等待 `libcocos2dcpp.so` 完成映射。
2. `GameVersionManager` 用已知的 `Java_low_moe_AppActivity_setAppVersion` 偏移判断候选版本。
3. 然后读取或 Hook 原生 `appVersion` setter / 全局，等游戏上报真实版本号。
4. 版本字符串与支持列表完全匹配后，才会安装 Hook。

不支持的版本会停在"已检测"状态，不会误用错误偏移。

## 新增版本

1. 在 `src/config/GameProfile.hpp` 中新增 `GameVersionId` 枚举值。
2. 填入版本探测偏移（`set_app_version` + `app_version_string` 全局）。
3. 填入该版本的 autoplay / network / ssl_pins 偏移。
4. 若对象布局发生变化，在 `src/config/GameStructs.hpp` 特化对应模板。
5. 更新本文档。

## 偏移组织

- **对象布局** — `src/config/GameStructs.hpp`（版本模板化，`offsetof` + `static_assert` 编译期校验）。
- **共享常量和字节签名** — `src/config/AutoplayConfig.h` / `src/config/NetworkBlockConfig.h`。
- **函数 / RTTI / patch 偏移** — `src/config/GameProfile.hpp`（每个支持版本一条记录）。
