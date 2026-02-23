#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace arc_autoplay::cfg::autoplay {

// Address constants used by autoplay hooks and binary patches.
// Values are image-base-relative offsets into `libcocos2dcpp.so`.

// Hook targets.
inline constexpr uintptr_t kLibcocos2dcpp_Gameplay_processLogicNotes = 0x147671C;
inline constexpr uintptr_t kLibcocos2dcpp_Gameplay_tryTapJudgementForTouch = 0x134BDA8;
inline constexpr uintptr_t kLibcocos2dcpp_ScoreState_applyJudgement = 0x0ACEEF4;
inline constexpr uintptr_t kLibcocos2dcpp_ScoreState_applyMiss = 0x0E532B0;

inline constexpr uintptr_t kLibcocos2dcpp_ShowJudgementEffectAtNote = 0x1253E98;
inline constexpr uintptr_t kLibcocos2dcpp_NoteEffect_onMiss = 0x1547754;
inline constexpr uintptr_t kLibcocos2dcpp_NoteEffect_onJudgement = 0x15D3180;
inline constexpr uintptr_t kLibcocos2dcpp_LogicColor_acceptsTouch = 0x116BA0C;

// Small inline instruction patches.
inline constexpr uintptr_t kLibcocos2dcpp_Patch_ProcessLogicNotes_add64_a = 0x1476C04;
inline constexpr uintptr_t kLibcocos2dcpp_Patch_ProcessLogicNotes_add64_b = 0x1476CBC;
inline constexpr uintptr_t kLibcocos2dcpp_Patch_ProcessLogicNotes_addC8 = 0x1476D0C;

// RTTI/typeinfo anchors.
inline constexpr uintptr_t kTypeinfo_LogicHoldNote = 0x18AAB60;
inline constexpr uintptr_t kTypeinfo_LogicArcNote = 0x18265D0;

// Gameplay object layout.
inline constexpr size_t kGameplay_timer_off = 48;
inline constexpr size_t kGameplay_note_begin_off = 160;
inline constexpr size_t kGameplay_note_end_off = 168;

// LogicNote / derived layout.
inline constexpr size_t kNote_active_u8_off = 84;
inline constexpr size_t kNote_timeStart_i32_off = 24;
inline constexpr size_t kNote_timeEnd_i32_off = 28;
inline constexpr size_t kNote_pos_ptr_off = 32;
inline constexpr size_t kNote_play_scene_ptr_off = 0x40;

// vtable offsets (byte offsets from vptr).
inline constexpr size_t kLogicNote_vcall_canApplyJudgement_off = 0x20;
inline constexpr size_t kLogicNote_vcall_setBeingTouched_off = 96;

inline constexpr size_t kNote_pos_xnorm_f32_off = 20;
inline constexpr size_t kNote_runtime_x_f32_off = 204;
inline constexpr size_t kNote_runtime_y_f32_off = 208;

inline constexpr size_t kLong_touch_state_u16_off = 92;
inline constexpr uint16_t kLong_touch_state_held = 0x0101;

inline constexpr size_t kArc_isVoid_i32_off = 156;
inline constexpr size_t kArc_activeNow_u8_off = 200;
inline constexpr size_t kArc_playScene_vcall_off = 0x530;

inline constexpr size_t kHold_headActivated_u8_off = 160;

// Timer layout.
inline constexpr size_t kTimer_flag_u8_off = 45;
inline constexpr size_t kTimer_msA_i32_off = 32;
inline constexpr size_t kTimer_msB_i32_off = 40;
inline constexpr size_t kTimer_msC_i32_off = 52;

// Touch-like layout used by synthetic stubs.
inline constexpr size_t kTouch_sys_id_i32_off = 0x0C;
inline constexpr size_t kTouch_ndc_x_f32_off = 0x1C;
inline constexpr size_t kTouch_ndc_y_f32_off = 0x20;
inline constexpr size_t kTouch_uid_i32_off = 0x34;
inline constexpr size_t kTouch_timestamp_i32_off = 0x3C;

// Runtime behavior knobs.
inline constexpr int kSynthTouchBaseId = 100;
inline constexpr int kMaxSynthTouches = 16;
inline constexpr int kSynthTouchHoldId = kSynthTouchBaseId + kMaxSynthTouches;
inline constexpr int kLongStartLeadMs = 0;
inline constexpr int kLongEndLagMs = 0;

// Track coordinate constants.
inline constexpr float kTrackHalfWidth = 425.0f;
inline constexpr float kTrackHeightHalf = 275.0f;
inline constexpr float kGroundYWorld = 100.0f;

// First 16 bytes at each hook target.
inline constexpr std::array<uint8_t, 16> kSig_Gameplay_processLogicNotes = {
    0xE8, 0x0F, 0x19, 0xFC,
    0xFD, 0x7B, 0x01, 0xA9,
    0xFC, 0x6F, 0x02, 0xA9,
    0xFA, 0x67, 0x03, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_ScoreState_applyJudgement = {
    0xFF, 0xC3, 0x02, 0xD1,
    0xEA, 0x23, 0x00, 0xFD,
    0xE9, 0x23, 0x05, 0x6D,
    0xFD, 0x7B, 0x06, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_ScoreState_applyMiss = {
    0xFF, 0xC3, 0x01, 0xD1,
    0xE8, 0x23, 0x00, 0xFD,
    0xFD, 0xFB, 0x04, 0xA9,
    0xF5, 0x2F, 0x00, 0xF9,
};

inline constexpr std::array<uint8_t, 16> kSig_Gameplay_tryTapJudgementForTouch = {
    0xFF, 0x43, 0x06, 0xD1,
    0xEA, 0x8B, 0x00, 0xFD,
    0xE9, 0x23, 0x12, 0x6D,
    0xFD, 0x7B, 0x13, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_ShowJudgementEffectAtNote = {
    0xFF, 0x83, 0x02, 0xD1,
    0xE8, 0x1B, 0x00, 0xFD,
    0xFD, 0x7B, 0x04, 0xA9,
    0xFC, 0x6F, 0x05, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_NoteEffect_onMiss = {
    0xFF, 0xC3, 0x01, 0xD1,
    0xE8, 0x1B, 0x00, 0xFD,
    0xFD, 0x7B, 0x04, 0xA9,
    0xF6, 0x57, 0x05, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_NoteEffect_onJudgement = {
    0xFF, 0x03, 0x02, 0xD1,
    0xE8, 0x1B, 0x00, 0xFD,
    0xFD, 0x7B, 0x04, 0xA9,
    0xF8, 0x5F, 0x05, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_LogicColor_acceptsTouch = {
    0xFF, 0xC3, 0x00, 0xD1,
    0xFD, 0x7B, 0x01, 0xA9,
    0xF4, 0x4F, 0x02, 0xA9,
    0xFD, 0x43, 0x00, 0x91,
};

} // namespace arc_autoplay::cfg::autoplay
