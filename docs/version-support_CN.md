# 版本支持

Language: [English](version-support.md) | 简体中文

## 当前支持版本

- `6.12.11c`
- `6.13.2f`
- `6.14.0c`
- `6.16.2c`（完整 profile；自定义谱面）
- `6.16.8c`（完整 profile；自定义谱面）
- `7.0.0c`（完整 profile；自定义谱面 —— note 家族派生布局整体 +8，见 `docs/offsets/7.0.0c-offsets_CN.md`）
- `7.0.1c`（完整 profile；自定义谱面 —— 沿用 7.0.x note 家族布局，见 `docs/offsets/7.0.1c-offsets_CN.md`）
- `7.0.255c`（完整 profile；自定义谱面 —— 沿用 7.0.x note 家族布局，见 `docs/offsets/7.0.255c-offsets_CN.md`）

## 运行时识别

1. Wrapper 等待 `libcocos2dcpp.so` 完成映射。
2. `GameVersionManager` 先读取各 profile 的 `appVersion` 字符串全局变量。
3. 如果全局变量尚未初始化，则通过 `dlsym` 动态解析并 Hook 导出的 `Java_low_moe_AppActivity_setAppVersion` 符号。
4. Hook 收到真实版本字符串后选择匹配的 profile；确认完成后才安装功能 Hook。

不支持的版本会停在"已检测"状态，不会误用错误偏移。

## 新增版本

1. 在 `src/game/GameProfile.hpp` 中新增 `GameVersionId` 枚举值。
2. 只需填入版本字符串全局变量偏移；`setAppVersion` 通过 ELF 导出符号解析，不再需要每版本偏移。
3. 填入该版本的 autoplay / network / ssl_pins / custom_charts 偏移与 capability。可使用 `scripts/port_match.py` 从上一版本的地址在新版库中定位函数偏移（工作流见其模块文档字符串）；`scripts/port_match_7000c_spec.json`、`scripts/port_match_7001c_spec.json` 与 `scripts/port_match_70255c_spec.json` 记录完整的已验证映射。
4. 若对象布局发生变化，在 `src/game/GameStructs.hpp` 特化对应模板。
5. 更新本文档。

## 偏移组织

- **对象布局** — `src/game/GameStructs.hpp`（版本模板化，`offsetof` + `static_assert` 编译期校验）。
- **共享常量和字节签名** — `src/config/AutoplayConfig.h`、`src/config/NetworkBlockConfig.h` 和 `src/config/CustomChartConfig.h`。
- **函数 / RTTI / patch 偏移** — `src/game/GameProfile.hpp`（每个支持版本一条记录）。
- **文档化位点** — `docs/offsets/6.12.11c-offsets.md`、`docs/offsets/6.16.2c-offsets.md`、`docs/offsets/6.16.8c-offsets.md`、`docs/offsets/7.0.0c-offsets_CN.md`、`docs/offsets/7.0.1c-offsets_CN.md` 和 `docs/offsets/7.0.255c-offsets_CN.md`。
