#pragma once

#include <array>
#include <cstring>
#include <cstdint>

#include "config/GameProfile.hpp"
#include "features/Feature.hpp"
#include "GameTypes.hpp"

namespace arc_autoplay {

// Autoplay feature.
//
// Implementation strategy:
// - Gameplay loop hook (`Gameplay_processLogicNotes`): drive long-note touch state
//   each tick (holds + arcs) by calling the note virtual `setBeingTouched`.
// - Score hooks (`ScoreState_applyJudgement` / `ScoreState_applyMiss`): coerce
//   grades to Perfect Pure and (when allowed) convert misses into judgements.
// - Effect hooks: simplify judgement/miss effects to reduce visual noise.
// - Small binary patches: relax some in-game touch acceptance logic.
//
// NOTE: This code is designed to be independent from network/offline logic.
class Autoplay : public Feature {
public:
    static Autoplay &Instance();

private:
    enum class TouchRole : uint8_t {
        None = 0,
        Arc,
    };

    // Minimal in-memory stand-in for the game's touch-like object.
    //
    // We do not attempt to mirror the full C++ type; we only write the fields
    // that the hooked functions actually read (sys_id/uid/timestamp/ndc coords).
    // Offsets are defined in `cfg::autoplay::*` and must match the game build.
    struct TouchLikeStub {
        alignas(8) uint8_t bytes[0x40];

        uintptr_t addr() const { return reinterpret_cast<uintptr_t>(bytes); }

        void Clear() { std::memset(bytes, 0, sizeof(bytes)); }

        void SetSysId(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::autoplay::kTouch_sys_id_i32_off, v);
        }

        void SetNdcXY(float x, float y) {
            mem::Write<float>(addr() + cfg::autoplay::kTouch_ndc_x_f32_off, x);
            mem::Write<float>(addr() + cfg::autoplay::kTouch_ndc_y_f32_off, y);
        }

        void SetTouchUid(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::autoplay::kTouch_uid_i32_off, v);
        }

        void SetTimestamp(int32_t v) {
            mem::Write<int32_t>(addr() + cfg::autoplay::kTouch_timestamp_i32_off, v);
        }
    };

    struct SynthTouch {
        // Stable synthetic ID range: [kSynthTouchBaseId, kSynthTouchHoldId].
        int sys_id = -1;
        TouchRole role = TouchRole::None;

        // For arc touches we keep a raw note handle and update its world->NDC
        // coords each tick.
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

    explicit Autoplay(bool enabled);

    void EnsureInstalled();
    void TryInstallHooks(const cfg::GameProfile &profile);

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
    //
    // This runs from the `Gameplay_processLogicNotes` hook.
    // It maintains the long-note "being touched" state so holds/arcs don't drop.
    void AutoplayLongNotesTick(game::Gameplay gameplay, int now_ms);
    void InitTouchesIfNeeded();
    int FindTouchByNote(game::LogicArcNote note) const;
    int FindFreeTouchIndex() const;
    void ReleaseTouch(SynthTouch &touch);
    void WriteTouchStub(int idx, int now_ms);

    // Call note virtual `setBeingTouched(touch_like, now_ms)`.
    // We guard the target vcall to avoid crashing on stale pointers.
    void CallNoteSetBeingTouched(game::LogicNote note, uintptr_t touch_like, int now_ms);

    bool IsSyntheticTouch(game::TouchLike touch) const;

    // Calls LogicNote::canApplyJudgement(judge_time, input_time) via vcall.
    // This is used as a safety gate before converting a miss into a judgement.
    bool NoteCanApplyJudgement(game::LogicNote note, int judge_time, int input_time) const;

    // A few internal vcall chains go through a play-scene/context pointer.
    // This helper verifies that the pointer looks sane and that the vcall target
    // address resolves into `libcocos2dcpp.so` executable ranges.
    bool ArcHasValidPlaySceneVcall(game::LogicArcNote arc_note) const;

    float WorldXToNdc(float x_world) const;
    float WorldYToNdc(float y_world) const;

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

    NoteEffectOnJudgementFn note_effect_on_judgement_ = nullptr;

    std::array<TouchLikeStub, cfg::autoplay::kMaxSynthTouches> touch_stubs_{};
    TouchLikeStub hold_touch_stub_{};
    std::array<SynthTouch, cfg::autoplay::kMaxSynthTouches> touches_{};
    bool touches_inited_ = false;
    bool hook_setup_done_ = false;
};

} // namespace arc_autoplay
