#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "game/GameStructs.hpp"

namespace arc_helper::cfg::autoplay {

// ---------------------------------------------------------------------------
//  Offsets computed from `layouts::*` mirror structs (see `GameStructs.hpp`).
//  All consumed fields are verified identical through 6.16.8c.
//  7.0.x shifts the note-family derived tail (see the Arc*/Hold*/Runtime
//  selectors below); base-zone offsets stay shared.
// ---------------------------------------------------------------------------
constexpr GameVersionId kLayoutVer = GameVersionId::k61211c;

inline constexpr size_t kGameplay_timer_off         = offsetof(layouts::Gameplay<kLayoutVer>, timer);
inline constexpr size_t kGameplay_note_begin_off    = offsetof(layouts::Gameplay<kLayoutVer>, note_begin);
inline constexpr size_t kGameplay_note_end_off      = offsetof(layouts::Gameplay<kLayoutVer>, note_end);

inline constexpr size_t kNote_active_u8_off         = offsetof(layouts::Note<kLayoutVer>, active);
inline constexpr size_t kNote_timeStart_i32_off     = offsetof(layouts::Note<kLayoutVer>, timeStart);
inline constexpr size_t kNote_timeEnd_i32_off       = offsetof(layouts::Note<kLayoutVer>, timeEnd);
inline constexpr size_t kNote_pos_ptr_off           = offsetof(layouts::Note<kLayoutVer>, pos_ptr);
inline constexpr size_t kNote_play_scene_ptr_off    = offsetof(layouts::Note<kLayoutVer>, playSceneCtx);
inline constexpr size_t kNote_runtime_x_f32_off     = offsetof(layouts::Note<kLayoutVer>, runtimeX);
inline constexpr size_t kNote_runtime_y_f32_off     = offsetof(layouts::Note<kLayoutVer>, runtimeY);
inline constexpr size_t kLong_touch_state_u16_off   = offsetof(layouts::Note<kLayoutVer>, longTouchState);

inline constexpr size_t kNote_pos_xnorm_f32_off     = offsetof(layouts::NotePosition<kLayoutVer>, xNorm);

inline constexpr size_t kArc_isVoid_i32_off         = offsetof(layouts::ArcNote<kLayoutVer>, isVoid);
inline constexpr size_t kArc_activeNow_u8_off       = offsetof(layouts::ArcNote<kLayoutVer>, activeNow);

inline constexpr size_t kHold_headActivated_u8_off  = offsetof(layouts::HoldNote<kLayoutVer>, headActivated);

inline constexpr size_t kTimer_flag_u8_off          = offsetof(layouts::Timer<kLayoutVer>, flag);
inline constexpr size_t kTimer_msA_i32_off          = offsetof(layouts::Timer<kLayoutVer>, msA);
inline constexpr size_t kTimer_msB_i32_off          = offsetof(layouts::Timer<kLayoutVer>, msB);
inline constexpr size_t kTimer_msC_i32_off          = offsetof(layouts::Timer<kLayoutVer>, msC);

inline constexpr size_t kTouch_sys_id_i32_off       = offsetof(layouts::TouchLike<kLayoutVer>, sysId);
inline constexpr size_t kTouch_ndc_x_f32_off        = offsetof(layouts::TouchLike<kLayoutVer>, ndcX);
inline constexpr size_t kTouch_ndc_y_f32_off        = offsetof(layouts::TouchLike<kLayoutVer>, ndcY);
inline constexpr size_t kTouch_uid_i32_off          = offsetof(layouts::TouchLike<kLayoutVer>, touchUid);
inline constexpr size_t kTouch_timestamp_i32_off    = offsetof(layouts::TouchLike<kLayoutVer>, timestamp);

// 7.0.x inserts one member into the note-family derived area; everything the
// game stores below those offsets keeps its address, everything after shifts
// by +8 (verified via matched instruction pairs in both builds).
inline constexpr size_t k7Arc_isVoid_i32_off           = 0xA4;
inline constexpr size_t k7Arc_activeNow_u8_off         = 0xD0;
inline constexpr size_t k7Note_runtime_x_f32_off       = 0xD4;
inline constexpr size_t k7Note_runtime_y_f32_off       = 0xD8;
inline constexpr size_t k7Hold_headActivated_u8_off    = 0xA8;

static_assert(k7Arc_isVoid_i32_off ==
              offsetof(layouts::ArcNote<GameVersionId::k7000c>, isVoid));
static_assert(k7Arc_activeNow_u8_off ==
              offsetof(layouts::ArcNote<GameVersionId::k7000c>, activeNow));
static_assert(k7Note_runtime_x_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k7000c>, runtimeX));
static_assert(k7Note_runtime_y_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k7000c>, runtimeY));
static_assert(k7Hold_headActivated_u8_off ==
              offsetof(layouts::HoldNote<GameVersionId::k7000c>, headActivated));
static_assert(k7Arc_isVoid_i32_off ==
              offsetof(layouts::ArcNote<GameVersionId::k7001c>, isVoid));
static_assert(k7Arc_activeNow_u8_off ==
              offsetof(layouts::ArcNote<GameVersionId::k7001c>, activeNow));
static_assert(k7Note_runtime_x_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k7001c>, runtimeX));
static_assert(k7Note_runtime_y_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k7001c>, runtimeY));
static_assert(k7Hold_headActivated_u8_off ==
              offsetof(layouts::HoldNote<GameVersionId::k7001c>, headActivated));
static_assert(k7Arc_isVoid_i32_off ==
              offsetof(layouts::ArcNote<GameVersionId::k70255c>, isVoid));
static_assert(k7Arc_activeNow_u8_off ==
              offsetof(layouts::ArcNote<GameVersionId::k70255c>, activeNow));
static_assert(k7Note_runtime_x_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k70255c>, runtimeX));
static_assert(k7Note_runtime_y_f32_off ==
              offsetof(layouts::NoteRuntimePos<GameVersionId::k70255c>, runtimeY));
static_assert(k7Hold_headActivated_u8_off ==
              offsetof(layouts::HoldNote<GameVersionId::k70255c>, headActivated));

inline constexpr bool Uses7NoteLayout(GameVersionId version) {
    return version == GameVersionId::k7000c || version == GameVersionId::k7001c ||
           version == GameVersionId::k70255c;
}

// Version-aware accessors for the shifted members. Callers pass the resolved
// profile id so that every read tracks the running build.
inline constexpr size_t ArcIsVoidOffset(GameVersionId version) {
    return Uses7NoteLayout(version) ? k7Arc_isVoid_i32_off : kArc_isVoid_i32_off;
}

inline constexpr size_t ArcActiveNowOffset(GameVersionId version) {
    return Uses7NoteLayout(version) ? k7Arc_activeNow_u8_off : kArc_activeNow_u8_off;
}

inline constexpr size_t NoteRuntimeXOffset(GameVersionId version) {
    return Uses7NoteLayout(version) ? k7Note_runtime_x_f32_off : kNote_runtime_x_f32_off;
}

inline constexpr size_t NoteRuntimeYOffset(GameVersionId version) {
    return Uses7NoteLayout(version) ? k7Note_runtime_y_f32_off : kNote_runtime_y_f32_off;
}

inline constexpr size_t HoldHeadActivatedOffset(GameVersionId version) {
    return Uses7NoteLayout(version) ? k7Hold_headActivated_u8_off : kHold_headActivated_u8_off;
}

// vtable offsets (byte offsets from vptr).
inline constexpr size_t kLogicNote_vcall_canApplyJudgement_off = 0x20;
inline constexpr size_t kLogicNote_vcall_setBeingTouched_off   = 96;
inline constexpr size_t kArc_playScene_vcall_off               = 0x530;

// Miss-window patch words shared by every supported profile: the three
// `add w8, w10, #imm` instructions zero their immediates, and the
// LogicColor_acceptsTouch gate is replaced by `mov w0, #0; ret`.
inline constexpr uint32_t kPatchExpectedAdd64A = 0x11019148u;
inline constexpr uint32_t kPatchExpectedAdd64B = 0x11019148u;
inline constexpr uint32_t kPatchExpectedAddC8  = 0x11032148u;
inline constexpr uint32_t kPatchImmediateMask  = 0x003FFC00u;
inline constexpr std::array<uint32_t, 2> kPatchColorGateReplacement = {
    0x52800020u,  // mov w0, #0
    0xD65F03C0u,  // ret
};

// Runtime behavior knobs.
inline constexpr int kSynthTouchBaseId = 100;
inline constexpr int kMaxSynthTouches = 16;
inline constexpr int kSynthTouchHoldId = kSynthTouchBaseId + kMaxSynthTouches;

// Track coordinate constants.
inline constexpr float kTrackHalfWidth = 425.0f;
inline constexpr float kTrackHeightHalf = 275.0f;

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

inline constexpr std::array<uint8_t, 16> kSig_6162c_ScoreState_applyJudgement = {
    0xFF, 0x83, 0x02, 0xD1,
    0xFD, 0x7B, 0x04, 0xA9,
    0xFB, 0x2B, 0x00, 0xF9,
    0xFA, 0x67, 0x06, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_ScoreState_applyMiss = {
    0xFF, 0xC3, 0x01, 0xD1,
    0xE8, 0x23, 0x00, 0xFD,
    0xFD, 0xFB, 0x04, 0xA9,
    0xF5, 0x2F, 0x00, 0xF9,
};

inline constexpr std::array<uint8_t, 16> kSig_6162c_ScoreState_applyMiss = {
    0xFF, 0x03, 0x02, 0xD1,
    0xFD, 0x7B, 0x04, 0xA9,
    0xF8, 0x5F, 0x05, 0xA9,
    0xF6, 0x57, 0x06, 0xA9,
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

inline constexpr const std::array<uint8_t, 16> &ScoreStateApplyJudgementSignature(GameVersionId version) {
    return version == GameVersionId::k61211c || version == GameVersionId::k6132f ||
                   version == GameVersionId::k6140c
               ? kSig_ScoreState_applyJudgement
               : kSig_6162c_ScoreState_applyJudgement;
}

inline constexpr const std::array<uint8_t, 16> &ScoreStateApplyMissSignature(GameVersionId version) {
    return version == GameVersionId::k61211c || version == GameVersionId::k6132f ||
                   version == GameVersionId::k6140c
               ? kSig_ScoreState_applyMiss
               : kSig_6162c_ScoreState_applyMiss;
}

} // namespace arc_helper::cfg::autoplay
