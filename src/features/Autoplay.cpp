#include "features/Autoplay.hpp"

#include "manager/GameVersionManager.hpp"
#include "utils/Log.h"

namespace arc_autoplay {

Autoplay &Autoplay::Instance() {
    if constexpr (!cfg::module::kAutoplayEnabled) {
        static Autoplay disabled(false);
        return disabled;
    } else {
        static Autoplay enabled(true);

        // Hooks are installed lazily (and only once) after `libcocos2dcpp.so`
        // is mapped and the base address can be resolved.
        enabled.EnsureInstalled();
        return enabled;
    }
}

Autoplay::Autoplay(bool enabled) {
    if (!enabled) return;
    EnsureInstalled();
}

void Autoplay::EnsureInstalled() {
    // Idempotent: safe to call repeatedly.
    if (hook_setup_done_) return;
    if (!RefreshLibBase()) return;
    auto &version_manager = GameVersionManager::Instance();
    version_manager.EnsureInstalled();

    const auto *profile = version_manager.GetActiveProfile();
    if (!profile) return;

    TryInstallHooks(*profile);
}

void Autoplay::InitTouchesIfNeeded() {
    if (touches_inited_) return;

    // Touch stubs are just raw memory blobs written using fixed offsets.
    // We clear them once and then overwrite the fields that change per tick.

    for (auto &stub : touch_stubs_) stub.Clear();
    hold_touch_stub_.Clear();

    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        touches_[i].sys_id = cfg::autoplay::kSynthTouchBaseId + i;
        touches_[i].role = TouchRole::None;
        touches_[i].note = {};
        touches_[i].x = 0.0f;
        touches_[i].y = 0.0f;
        touches_[i].z = 0.0f;
    }

    touches_inited_ = true;
}

float Autoplay::WorldXToNdc(float x_world) const {
    // Convert "track-space" x to normalized device coords.
    return x_world / cfg::autoplay::kTrackHalfWidth;
}

float Autoplay::WorldYToNdc(float y_world) const {
    // Convert "track-space" y to normalized device coords.
    // The track origin is centered; NDC expects [-1, 1] with -1 at bottom.
    return (y_world / cfg::autoplay::kTrackHeightHalf) - 1.0f;
}

int Autoplay::FindTouchByNote(game::LogicArcNote note) const {
    const uintptr_t want = note.addr();
    if (!want) return -1;
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::None) continue;
        if (touches_[i].note.addr() == want) return i;
    }
    return -1;
}

int Autoplay::FindFreeTouchIndex() const {
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::None) return i;
    }
    return -1;
}

void Autoplay::ReleaseTouch(SynthTouch &touch) {
    touch.role = TouchRole::None;
    touch.note = {};
}

void Autoplay::WriteTouchStub(int idx, int now_ms) {
    if (idx < 0 || idx >= cfg::autoplay::kMaxSynthTouches) return;

    const auto &touch = touches_[idx];
    auto &stub = touch_stubs_[idx];

    stub.SetSysId(touch.sys_id);
    stub.SetNdcXY(touch.x, touch.y);
    stub.SetTouchUid(touch.sys_id);
    stub.SetTimestamp(now_ms);
}

void Autoplay::CallNoteSetBeingTouched(game::LogicNote note, uintptr_t touch_like, int now_ms) {
    if (!note || !touch_like) return;

    // Call a virtual method from an object potentially managed by the game.
    // This guard is critical for stability when switching charts/replays.
    const uintptr_t fn = note.vcall(cfg::autoplay::kLogicNote_vcall_setBeingTouched_off);
    if (!fn || !mem::IsAddrInLibraryExec(fn, cfg::module::kLibName)) return;

    using Fn = void (*)(uintptr_t, uintptr_t, int);
    reinterpret_cast<Fn>(fn)(note.addr(), touch_like, now_ms);
}

bool Autoplay::ArcHasValidPlaySceneVcall(game::LogicArcNote arc_note) const {
    if (!arc_note) return false;

    const uintptr_t ctx = arc_note.playSceneOrCtx();
    if (!ctx || (ctx & 7) != 0 || !mem::ProcMaps::IsReadable(ctx, sizeof(uintptr_t))) return false;

    const uintptr_t vtbl = mem::Read<uintptr_t>(ctx);
    if (!vtbl || (vtbl & 7) != 0) return false;

    const uintptr_t fn_addr = vtbl + cfg::autoplay::kArc_playScene_vcall_off;
    if (!mem::ProcMaps::IsReadable(fn_addr, sizeof(uintptr_t))) return false;

    const uintptr_t fn = mem::Read<uintptr_t>(fn_addr);
    return mem::IsAddrInLibraryExec(fn, cfg::module::kLibName);
}

void Autoplay::AutoplayLongNotesTick(game::Gameplay gameplay, int now_ms) {
    if (!lib_base_ || !gameplay) return;

    const auto *profile = GameVersionManager::Instance().GetActiveProfile();
    if (!profile) return;

    InitTouchesIfNeeded();

    // This build only keeps arc assignments in the touch pool.
    // Holds use a dedicated stub (`hold_touch_stub_`) and are not tracked.
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role != TouchRole::Arc) {
            ReleaseTouch(touches_[i]);
        }
    }

    auto *note_begin = gameplay.pendingNoteBegin();
    auto *note_end = gameplay.pendingNoteEnd();
    if (!note_begin || !note_end || note_begin == note_end) {
        for (auto &touch : touches_) {
            if (touch.role != TouchRole::None) {
                ReleaseTouch(touch);
            }
        }
        return;
    }

    const uintptr_t ti_arc = lib_base_ + profile->autoplay.typeinfo_logic_arc_note;
    const uintptr_t ti_hold = lib_base_ + profile->autoplay.typeinfo_logic_hold_note;

    hold_touch_stub_.SetSysId(cfg::autoplay::kSynthTouchHoldId);
    hold_touch_stub_.SetNdcXY(0.0f, 0.0f);
    hold_touch_stub_.SetTouchUid(cfg::autoplay::kSynthTouchHoldId);
    hold_touch_stub_.SetTimestamp(now_ms);

    // Per-frame long-note touch flag reset.
    // The game uses this state to decide whether a long note is currently held.
    for (auto p = note_begin; p != note_end; ++p) {
        game::LogicNote note(*p);
        if (!note.active()) continue;

        const uintptr_t typeinfo = note.typeinfo();
        if (typeinfo != ti_arc && typeinfo != ti_hold) continue;
        note.clearLongTouchState();
    }

    std::array<uint8_t, cfg::autoplay::kMaxSynthTouches> arc_seen{};

    for (auto p = note_begin; p != note_end; ++p) {
        game::LogicNote note(*p);
        if (!note.active()) continue;

        const uintptr_t typeinfo = note.typeinfo();

        if (typeinfo == ti_hold) {
            // Holds: mark head activated while within (t0, t1) and keep it touched.
            game::LogicHoldNote hold(note.addr());
            const int t0 = hold.timeStart();
            const int t1 = hold.timeEnd();
            if (now_ms > t0 && now_ms < t1) {
                hold.setHeadActivated(1);
                CallNoteSetBeingTouched(hold, hold_touch_stub_.addr(), now_ms);
            }
            continue;
        }

        if (typeinfo != ti_arc) continue;

        game::LogicArcNote arc(note.addr());
        const int t0 = arc.timeStart();
        const int t1 = arc.timeEnd();

        // Arcs: ignore void arcs and arcs that are inactive.
        if (arc.isVoid()) continue;
        if (!arc.activeNow()) continue;
        if (now_ms < t0 - cfg::autoplay::kLongStartLeadMs) continue;
        if (now_ms > t1 + cfg::autoplay::kLongEndLagMs) continue;

        int idx = FindTouchByNote(arc);
        if (idx < 0) {
            // Allocate a synthetic finger for this arc.
            idx = FindFreeTouchIndex();
            if (idx < 0) continue;
            touches_[idx].note = arc;
            touches_[idx].role = TouchRole::Arc;
        }

        const auto pos = arc.pos();
        if (!pos) continue;
        const float x_norm = pos.x_norm();

        // Arcaea stores x as a normalized [0..1] value over the track width.
        // Convert that to world-space, then to NDC.
        const float x_base_world = x_norm * (2.0f * cfg::autoplay::kTrackHalfWidth) - cfg::autoplay::kTrackHalfWidth;
        const float x_world = x_base_world + arc.runtimeX();
        const float y_world = arc.runtimeY();

        touches_[idx].x = WorldXToNdc(x_world);
        touches_[idx].y = WorldYToNdc(y_world);
        touches_[idx].z = 0.0f;
        arc_seen[idx] = 1;
    }

    // Guard against stale arc pointers when switching charts/replays.
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::Arc && !arc_seen[i]) {
            ReleaseTouch(touches_[i]);
        }
    }

    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        auto &touch = touches_[i];
        if (touch.role != TouchRole::Arc) continue;
        if (!arc_seen[i]) continue;
        if (!touch.note) continue;

        if (touch.note.isVoid() || !touch.note.activeNow()) {
            ReleaseTouch(touch);
            continue;
        }

        if (!ArcHasValidPlaySceneVcall(touch.note)) {
            ReleaseTouch(touch);
            continue;
        }

        WriteTouchStub(i, now_ms);
        CallNoteSetBeingTouched(touch.note, touch_stubs_[i].addr(), now_ms);
    }
}

bool Autoplay::IsSyntheticTouch(game::TouchLike touch) const {
    if (!touch) return false;
    const int sys_id = touch.sys_id();
    return sys_id >= cfg::autoplay::kSynthTouchBaseId && sys_id <= cfg::autoplay::kSynthTouchHoldId;
}

bool Autoplay::NoteCanApplyJudgement(game::LogicNote note, int judge_time, int input_time) const {
    if (!note) return false;

    const uintptr_t gate_fn = note.vcall(cfg::autoplay::kLogicNote_vcall_canApplyJudgement_off);
    if (!gate_fn || !mem::IsAddrInLibraryExec(gate_fn, cfg::module::kLibName)) return false;

    using GateFn = bool (*)(uintptr_t, int, int);
    return reinterpret_cast<GateFn>(gate_fn)(note.addr(), judge_time, input_time);
}

void Autoplay::OnGameplayProcessLogicNotes(game::Gameplay gameplay, uintptr_t play_scene_or_ctx) {
    // This hook runs every logic tick and is our main timebase.
    const int now_ms = gameplay.timer().NowMs();
    last_now_ms_ = now_ms;

    AutoplayLongNotesTick(gameplay, now_ms);

    CALL_ORIG(GameplayProcessLogicNotesHook, gameplay.addr(), play_scene_or_ctx);
}

void Autoplay::OnGameplayTryTapJudgementForTouch(game::Gameplay gameplay, game::TouchLike touch, int lane_hint) {
    if (!touch) return;

    // Only forward judgements for our synthetic touches. Real player touches
    // are left alone.
    if (!IsSyntheticTouch(touch)) return;

    CALL_ORIG(GameplayTryTapJudgementForTouchHook, gameplay.addr(), touch.addr(), lane_hint);
}

void Autoplay::OnScoreStateApplyJudgement(uintptr_t score,
                                          uintptr_t note,
                                          int grade,
                                          int timing,
                                          int judge_time,
                                          int input_time) {
    // Force score grade/timing to Perfect Pure.
    (void)grade;
    (void)timing;
    constexpr int kJG_PERFECT_PURE = 0;
    constexpr int kJT_NONE = 0;
    CALL_ORIG(ScoreStateApplyJudgementHook, score, note, kJG_PERFECT_PURE, kJT_NONE, judge_time, input_time);
}

void Autoplay::OnScoreStateApplyMiss(uintptr_t score, uintptr_t note) {
    if (!addr_score_state_apply_judgement_) {
        CALL_ORIG(ScoreStateApplyMissHook, score, note);
        return;
    }

    const game::LogicNote logic_note(note);

    constexpr int kJG_PERFECT_PURE = 0;
    constexpr int kJT_NONE = 0;
    constexpr int kInputTimeSynth = -1;

    int judge_time = last_now_ms_;
    if (judge_time == 0 && logic_note) {
        judge_time = logic_note.timeStart();
    }

    // Convert a miss into a Perfect Pure judgement when the game says
    // it's valid to apply judgement at this time.
    if (!NoteCanApplyJudgement(logic_note, judge_time, kInputTimeSynth)) {
        CALL_ORIG(ScoreStateApplyMissHook, score, note);
        return;
    }

    CALL_ORIG(ScoreStateApplyJudgementHook,
              score,
              note,
              kJG_PERFECT_PURE,
              kJT_NONE,
              judge_time,
              kInputTimeSynth);
}

void Autoplay::OnShowJudgementEffectAtNote(uintptr_t ctx,
                                           uintptr_t note,
                                           uintptr_t pos_ptr,
                                           unsigned int kind,
                                           int early_late) {
    (void)note;
    (void)pos_ptr;

    // Collapse all judgement effects into a single consistent effect.
    kind = 0;
    early_late = 0;
    CALL_ORIG(ShowJudgementEffectAtNoteHook, ctx, note, pos_ptr, kind, early_late);
}

int64_t Autoplay::OnNoteEffectOnMiss(uintptr_t self, uintptr_t note) {
    if (note_effect_on_judgement_) {
        return note_effect_on_judgement_(self, note, 0, 0);
    }
    return CALL_ORIG(NoteEffectOnMissHook, self, note);
}

void Autoplay::GameplayProcessLogicNotesHook(uintptr_t gameplay, uintptr_t play_scene_or_ctx) {
    Instance().OnGameplayProcessLogicNotes(game::Gameplay(gameplay), play_scene_or_ctx);
}

void Autoplay::GameplayTryTapJudgementForTouchHook(uintptr_t gameplay, uintptr_t touch, int lane_hint) {
    Instance().OnGameplayTryTapJudgementForTouch(game::Gameplay(gameplay), game::TouchLike(touch), lane_hint);
}

void Autoplay::ScoreStateApplyJudgementHook(uintptr_t score,
                                            uintptr_t note,
                                            int grade,
                                            int timing,
                                            int judge_time,
                                            int input_time) {
    Instance().OnScoreStateApplyJudgement(score, note, grade, timing, judge_time, input_time);
}

void Autoplay::ScoreStateApplyMissHook(uintptr_t score, uintptr_t note) {
    Instance().OnScoreStateApplyMiss(score, note);
}

void Autoplay::ShowJudgementEffectAtNoteHook(uintptr_t ctx,
                                             uintptr_t note,
                                             uintptr_t pos_ptr,
                                             unsigned int kind,
                                             int early_late) {
    Instance().OnShowJudgementEffectAtNote(ctx, note, pos_ptr, kind, early_late);
}

int64_t Autoplay::NoteEffectOnMissHook(uintptr_t self, uintptr_t note) {
    return Instance().OnNoteEffectOnMiss(self, note);
}

void Autoplay::TryInstallHooks(const cfg::GameProfile &profile) {
    if (!lib_base_) return;

    const auto &offsets = profile.autoplay;

    if (!patched_logicnote_miss_offsets_) {
        // Patch small immediates in the logic note processing loop.
        // Offsets are provided by the active game profile.
        const uintptr_t p64a = lib_base_ + offsets.patch_process_logic_notes_add64_a;
        const uintptr_t p64b = lib_base_ + offsets.patch_process_logic_notes_add64_b;
        const uintptr_t pc8 = lib_base_ + offsets.patch_process_logic_notes_addc8;
        const bool ok = mem::Patcher::PatchA64ClearImm12(p64a, 0x11019148) &&
                        mem::Patcher::PatchA64ClearImm12(p64b, 0x11019148) &&
                        mem::Patcher::PatchA64ClearImm12(pc8, 0x11032148);
        patched_logicnote_miss_offsets_ = ok;
        ARC_LOGI("LogicNotes miss offsets patch: %s", ok ? "OK" : "FAILED");
    }

    hook_manager_.InstallInlineHook(addr_score_state_apply_judgement_,
                                    offsets.score_state_apply_judgement,
                                    cfg::autoplay::kSig_ScoreState_applyJudgement,
                                    ScoreStateApplyJudgementHook,
                                    "ScoreState_applyJudgement");

    hook_manager_.InstallInlineHook(addr_score_state_apply_miss_,
                                    offsets.score_state_apply_miss,
                                    cfg::autoplay::kSig_ScoreState_applyMiss,
                                    ScoreStateApplyMissHook,
                                    "ScoreState_applyMiss");

    hook_manager_.InstallInlineHook(addr_gameplay_process_logic_notes_,
                                    offsets.gameplay_process_logic_notes,
                                    cfg::autoplay::kSig_Gameplay_processLogicNotes,
                                    GameplayProcessLogicNotesHook,
                                    "Gameplay_processLogicNotes");

    hook_manager_.InstallInlineHook(addr_show_judgement_effect_at_note_,
                                    offsets.show_judgement_effect_at_note,
                                    cfg::autoplay::kSig_ShowJudgementEffectAtNote,
                                    ShowJudgementEffectAtNoteHook,
                                    "ShowJudgementEffectAtNote");

    hook_manager_.InstallInlineHook(addr_gameplay_try_tap_judgement_for_touch_,
                                    offsets.gameplay_try_tap_judgement_for_touch,
                                    cfg::autoplay::kSig_Gameplay_tryTapJudgementForTouch,
                                    GameplayTryTapJudgementForTouchHook,
                                    "Gameplay_tryTapJudgementForTouch");

    if (!patched_logiccolor_accepts_touch_ &&
        hook_manager_.ResolveAddress(addr_logic_color_accepts_touch_,
                                     offsets.logic_color_accepts_touch,
                                     cfg::autoplay::kSig_LogicColor_acceptsTouch,
                                     "LogicColor_acceptsTouch")) {
        // Force LogicColor::acceptsTouch() to always return true:
        //   mov w0, #1
        //   ret
        const bool ok = mem::Patcher::PatchA64ReturnTrue(addr_logic_color_accepts_touch_);
        patched_logiccolor_accepts_touch_ = ok;
        ARC_LOGI("Patched LogicColor_acceptsTouch @ %p: %s",
                 reinterpret_cast<void *>(addr_logic_color_accepts_touch_),
                 ok ? "OK" : "FAILED");
    }

    hook_manager_.ResolveFunctionPtr(addr_note_effect_on_judgement_,
                                     offsets.note_effect_on_judgement,
                                     cfg::autoplay::kSig_NoteEffect_onJudgement,
                                     note_effect_on_judgement_,
                                     "NoteEffect_onJudgement");

    hook_manager_.InstallInlineHook(addr_note_effect_on_miss_,
                                    offsets.note_effect_on_miss,
                                    cfg::autoplay::kSig_NoteEffect_onMiss,
                                    NoteEffectOnMissHook,
                                    "NoteEffect_onMiss");

    const bool hooks_ready =
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(ScoreStateApplyJudgementHook)) &&
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(ScoreStateApplyMissHook)) &&
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(GameplayProcessLogicNotesHook)) &&
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(ShowJudgementEffectAtNoteHook)) &&
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(GameplayTryTapJudgementForTouchHook)) &&
        hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(NoteEffectOnMissHook));

    hook_setup_done_ = hooks_ready &&
                       patched_logicnote_miss_offsets_ &&
                       patched_logiccolor_accepts_touch_ &&
                       note_effect_on_judgement_ != nullptr;
}

} // namespace arc_autoplay
