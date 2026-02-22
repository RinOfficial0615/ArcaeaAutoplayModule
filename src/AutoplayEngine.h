#pragma once

#include <array>
#include <cstring>
#include <cstdint>

#include "GameTypes.h"
#include "MemoryUtils.h"

namespace arc_autoplay {

class AutoplayEngine {
public:
    static AutoplayEngine &Instance();

    void OnLibLoaded(uintptr_t lib_base);

    static bool IsTargetPackage(const char *pkg);

private:
    enum class TouchRole : uint8_t {
        None = 0,
        Arc,
    };

    struct TouchLikeStub {
        alignas(8) uint8_t bytes[0x40];

        uintptr_t addr() const { return reinterpret_cast<uintptr_t>(bytes); }

        void Clear() { std::memset(bytes, 0, sizeof(bytes)); }

        void SetSysId(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::kTouch_sys_id_i32_off, v);
        }

        void SetNdcXY(float x, float y) {
            mem::Write<float>(addr() + cfg::kTouch_ndc_x_f32_off, x);
            mem::Write<float>(addr() + cfg::kTouch_ndc_y_f32_off, y);
        }

        void SetTouchUid(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::kTouch_uid_i32_off, v);
        }

        void SetTimestamp(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::kTouch_timestamp_i32_off, v);
        }
    };

    struct SynthTouch {
        int sys_id = -1;
        TouchRole role = TouchRole::None;
        game::LogicArcNote note{};
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    using GameplayProcessLogicNotesFn = void (*)(uintptr_t gameplay, uintptr_t play_scene_or_ctx);
    using GameplayTryTapJudgementForTouchFn = void (*)(uintptr_t gameplay, uintptr_t touch, int lane_hint);
    using ScoreStateApplyJudgementFn = void (*)(uintptr_t score,
                                                uintptr_t note,
                                                int grade,
                                                int timing,
                                                int judge_time,
                                                int input_time);
    using ScoreStateApplyMissFn = void (*)(uintptr_t score, uintptr_t note);
    using ShowJudgementEffectAtNoteFn = void (*)(uintptr_t ctx,
                                                uintptr_t note,
                                                uintptr_t pos_ptr,
                                                unsigned int kind,
                                                int early_late);
    using NoteEffectOnJudgementFn = int64_t (*)(uintptr_t self, uintptr_t note, unsigned int kind, int early_late);
    using NoteEffectOnMissFn = int64_t (*)(uintptr_t self, uintptr_t note);

    AutoplayEngine() = default;

    void TryInstallAutoplayHooks();

    // Hook callbacks.
    void OnGameplayProcessLogicNotes(game::Gameplay gameplay, uintptr_t play_scene_or_ctx);
    void OnGameplayTryTapJudgementForTouch(game::Gameplay gameplay, game::TouchLike touch, int lane_hint);
    void OnScoreStateApplyJudgement(uintptr_t score,
                                    uintptr_t note,
                                    int grade,
                                    int timing,
                                    int judge_time,
                                    int input_time);
    void OnScoreStateApplyMiss(uintptr_t score, uintptr_t note);
    void OnShowJudgementEffectAtNote(uintptr_t ctx,
                                     uintptr_t note,
                                     uintptr_t pos_ptr,
                                     unsigned int kind,
                                     int early_late);
    int64_t OnNoteEffectOnMiss(uintptr_t self, uintptr_t note);

    static void GameplayProcessLogicNotesHook(uintptr_t gameplay, uintptr_t play_scene_or_ctx);
    static void GameplayTryTapJudgementForTouchHook(uintptr_t gameplay, uintptr_t touch, int lane_hint);
    static void ScoreStateApplyJudgementHook(uintptr_t score,
                                             uintptr_t note,
                                             int grade,
                                             int timing,
                                             int judge_time,
                                             int input_time);
    static void ScoreStateApplyMissHook(uintptr_t score, uintptr_t note);
    static void ShowJudgementEffectAtNoteHook(uintptr_t ctx,
                                              uintptr_t note,
                                              uintptr_t pos_ptr,
                                              unsigned int kind,
                                              int early_late);
    static int64_t NoteEffectOnMissHook(uintptr_t self, uintptr_t note);

    // Long-note automation.
    void AutoplayLongNotesTick(game::Gameplay gameplay, int now_ms);
    void InitTouchesIfNeeded();
    int FindTouchByNote(game::LogicArcNote note) const;
    int FindFreeTouchIndex() const;
    void ReleaseTouch(SynthTouch &touch);
    void WriteTouchStub(int idx, int now_ms);
    void CallNoteSetBeingTouched(game::LogicNote note, uintptr_t touch_like, int now_ms);

    bool IsSyntheticTouch(game::TouchLike touch) const;
    bool NoteCanApplyJudgement(game::LogicNote note, int judge_time, int input_time) const;
    bool ArcHasValidPlaySceneVcall(game::LogicArcNote arc_note) const;

    float WorldXToNdc(float x_world) const;
    float WorldYToNdc(float y_world) const;

    bool ResolveAddr(uintptr_t &cached_addr,
                     uintptr_t hint_offset,
                     const std::array<uint8_t, 16> &sig,
                     const char *name);

    mem::AddressResolver resolver_;
    uintptr_t lib_base_ = 0;

    uintptr_t addr_gameplay_process_logic_notes_ = 0;
    uintptr_t addr_gameplay_try_tap_judgement_for_touch_ = 0;
    uintptr_t addr_score_state_apply_judgement_ = 0;
    uintptr_t addr_score_state_apply_miss_ = 0;
    uintptr_t addr_show_judgement_effect_at_note_ = 0;
    uintptr_t addr_note_effect_on_miss_ = 0;
    uintptr_t addr_note_effect_on_judgement_ = 0;
    uintptr_t addr_logic_color_accepts_touch_ = 0;

    bool patched_logicnote_miss_offsets_ = false;
    bool patched_logiccolor_accepts_touch_ = false;
    int last_now_ms_ = 0;

    GameplayProcessLogicNotesFn orig_gameplay_process_logic_notes_ = nullptr;
    GameplayTryTapJudgementForTouchFn orig_gameplay_try_tap_judgement_for_touch_ = nullptr;
    ScoreStateApplyJudgementFn orig_score_state_apply_judgement_ = nullptr;
    ScoreStateApplyMissFn orig_score_state_apply_miss_ = nullptr;
    ShowJudgementEffectAtNoteFn orig_show_judgement_effect_at_note_ = nullptr;
    NoteEffectOnJudgementFn note_effect_on_judgement_ = nullptr;
    NoteEffectOnMissFn orig_note_effect_on_miss_ = nullptr;

    std::array<TouchLikeStub, cfg::kMaxSynthTouches> touch_stubs_{};
    TouchLikeStub hold_touch_stub_{};
    std::array<SynthTouch, cfg::kMaxSynthTouches> touches_{};
    bool touches_inited_ = false;
};

} // namespace arc_autoplay
