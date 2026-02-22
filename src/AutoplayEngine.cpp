#include "AutoplayEngine.h"

#include <cstring>
#include <type_traits>

#include "Log.h"

namespace arc_autoplay {

AutoplayEngine &AutoplayEngine::Instance() {
    static AutoplayEngine engine;
    return engine;
}

void AutoplayEngine::OnLibLoaded(uintptr_t lib_base) {
    if (!lib_base) return;
    lib_base_ = lib_base;
    resolver_.SetLibBase(lib_base);
    TryInstallAutoplayHooks();
}

bool AutoplayEngine::IsTargetPackage(const char *pkg) {
    return pkg &&
           (strcmp(pkg, "moe.inf.arc") == 0 || strcmp(pkg, "moe.low.arc") == 0 || strcmp(pkg, "moe.low.mes") == 0);
}

bool AutoplayEngine::ResolveAddr(uintptr_t &cached_addr,
                                 uintptr_t hint_offset,
                                 const std::array<uint8_t, 16> &sig,
                                 const char *name) {
    if (cached_addr) return true;

    cached_addr = resolver_.ResolveBySignature(hint_offset, sig.data(), sig.size(), cfg::kLibName);
    if (cached_addr) {
        ARC_LOGI("Resolved %s @ %p", name, reinterpret_cast<void *>(cached_addr));
        return true;
    }

    ARC_LOGE("Failed to resolve %s", name);
    return false;
}

void AutoplayEngine::InitTouchesIfNeeded() {
    if (touches_inited_) return;

    for (auto &stub : touch_stubs_) stub.Clear();
    hold_touch_stub_.Clear();

    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
        touches_[i].sys_id = cfg::kSynthTouchBaseId + i;
        touches_[i].role = TouchRole::None;
        touches_[i].note = {};
        touches_[i].x = 0.0f;
        touches_[i].y = 0.0f;
        touches_[i].z = 0.0f;
    }

    touches_inited_ = true;
}

float AutoplayEngine::WorldXToNdc(float x_world) const {
    return x_world / cfg::kTrackHalfWidth;
}

float AutoplayEngine::WorldYToNdc(float y_world) const {
    return (y_world / cfg::kTrackHeightHalf) - 1.0f;
}

int AutoplayEngine::FindTouchByNote(game::LogicArcNote note) const {
    const uintptr_t want = note.addr();
    if (!want) return -1;
    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::None) continue;
        if (touches_[i].note.addr() == want) return i;
    }
    return -1;
}

int AutoplayEngine::FindFreeTouchIndex() const {
    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::None) return i;
    }
    return -1;
}

void AutoplayEngine::ReleaseTouch(SynthTouch &touch) {
    touch.role = TouchRole::None;
    touch.note = {};
}

void AutoplayEngine::WriteTouchStub(int idx, int now_ms) {
    if (idx < 0 || idx >= cfg::kMaxSynthTouches) return;

    const auto &touch = touches_[idx];
    auto &stub = touch_stubs_[idx];

    stub.SetSysId(touch.sys_id);
    stub.SetNdcXY(touch.x, touch.y);
    stub.SetTouchUid(touch.sys_id);
    stub.SetTimestamp(now_ms);
}

void AutoplayEngine::CallNoteSetBeingTouched(game::LogicNote note, uintptr_t touch_like, int now_ms) {
    if (!note || !touch_like) return;

    const uintptr_t fn = note.vcall(cfg::kLogicNote_vcall_setBeingTouched_off);
    if (!fn || !mem::IsAddrInLibraryExec(fn, cfg::kLibName)) return;

    using Fn = void (*)(uintptr_t, uintptr_t, int);
    reinterpret_cast<Fn>(fn)(note.addr(), touch_like, now_ms);
}

bool AutoplayEngine::ArcHasValidPlaySceneVcall(game::LogicArcNote arc_note) const {
    if (!arc_note) return false;

    const uintptr_t ctx = arc_note.playSceneOrCtx();
    if (!ctx || (ctx & 7) != 0) return false;

    const uintptr_t vtbl = mem::Read<uintptr_t>(ctx);
    if (!vtbl || (vtbl & 7) != 0) return false;

    const uintptr_t fn = mem::Read<uintptr_t>(vtbl + cfg::kArc_playScene_vcall_off);
    return mem::IsAddrInLibraryExec(fn, cfg::kLibName);
}

void AutoplayEngine::AutoplayLongNotesTick(game::Gameplay gameplay, int now_ms) {
    if (!lib_base_ || !gameplay) return;

    InitTouchesIfNeeded();

    // This build only keeps arc assignments in the touch pool.
    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
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

    const uintptr_t ti_arc = lib_base_ + cfg::kTypeinfo_LogicArcNote;
    const uintptr_t ti_hold = lib_base_ + cfg::kTypeinfo_LogicHoldNote;

    hold_touch_stub_.SetSysId(cfg::kSynthTouchHoldId);
    hold_touch_stub_.SetNdcXY(0.0f, 0.0f);
    hold_touch_stub_.SetTouchUid(cfg::kSynthTouchHoldId);
    hold_touch_stub_.SetTimestamp(now_ms);

    // Per-frame long-note touch flag reset.
    for (auto p = note_begin; p != note_end; ++p) {
        game::LogicNote note(*p);
        if (!note.active()) continue;

        const uintptr_t typeinfo = note.typeinfo();
        if (typeinfo != ti_arc && typeinfo != ti_hold) continue;
        note.clearLongTouchState();
    }

    std::array<uint8_t, cfg::kMaxSynthTouches> arc_seen{};

    for (auto p = note_begin; p != note_end; ++p) {
        game::LogicNote note(*p);
        if (!note.active()) continue;

        const uintptr_t typeinfo = note.typeinfo();

        if (typeinfo == ti_hold) {
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

        if (arc.isVoid()) continue;
        if (!arc.activeNow()) continue;
        if (now_ms < t0 - cfg::kLongStartLeadMs) continue;
        if (now_ms > t1 + cfg::kLongEndLagMs) continue;

        int idx = FindTouchByNote(arc);
        if (idx < 0) {
            idx = FindFreeTouchIndex();
            if (idx < 0) continue;
            touches_[idx].note = arc;
            touches_[idx].role = TouchRole::Arc;
        }

        const auto pos = arc.pos();
        if (!pos) continue;
        const float x_norm = pos.x_norm();

        const float x_base_world = x_norm * (2.0f * cfg::kTrackHalfWidth) - cfg::kTrackHalfWidth;
        const float x_world = x_base_world + arc.runtimeX();
        const float y_world = arc.runtimeY();

        touches_[idx].x = WorldXToNdc(x_world);
        touches_[idx].y = WorldYToNdc(y_world);
        touches_[idx].z = 0.0f;
        arc_seen[idx] = 1;
    }

    // Guard against stale arc pointers when switching charts/replays.
    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::Arc && !arc_seen[i]) {
            ReleaseTouch(touches_[i]);
        }
    }

    for (int i = 0; i < cfg::kMaxSynthTouches; ++i) {
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

bool AutoplayEngine::IsSyntheticTouch(game::TouchLike touch) const {
    if (!touch) return false;
    const int sys_id = touch.sys_id();
    return sys_id >= cfg::kSynthTouchBaseId && sys_id <= cfg::kSynthTouchHoldId;
}

bool AutoplayEngine::NoteCanApplyJudgement(game::LogicNote note, int judge_time, int input_time) const {
    if (!note) return false;

    const uintptr_t gate_fn = note.vcall(cfg::kLogicNote_vcall_canApplyJudgement_off);
    if (!gate_fn || !mem::IsAddrInLibraryExec(gate_fn, cfg::kLibName)) return false;

    using GateFn = bool (*)(uintptr_t, int, int);
    return reinterpret_cast<GateFn>(gate_fn)(note.addr(), judge_time, input_time);
}

void AutoplayEngine::OnGameplayProcessLogicNotes(game::Gameplay gameplay, uintptr_t play_scene_or_ctx) {
    const int now_ms = gameplay.timer().NowMs();
    last_now_ms_ = now_ms;

    AutoplayLongNotesTick(gameplay, now_ms);

    if (orig_gameplay_process_logic_notes_) {
        orig_gameplay_process_logic_notes_(gameplay.addr(), play_scene_or_ctx);
    }
}

void AutoplayEngine::OnGameplayTryTapJudgementForTouch(game::Gameplay gameplay, game::TouchLike touch, int lane_hint) {
    if (!orig_gameplay_try_tap_judgement_for_touch_) return;
    if (!touch) return;
    if (!IsSyntheticTouch(touch)) return;

    orig_gameplay_try_tap_judgement_for_touch_(gameplay.addr(), touch.addr(), lane_hint);
}

void AutoplayEngine::OnScoreStateApplyJudgement(uintptr_t score,
                                                uintptr_t note,
                                                int grade,
                                                int timing,
                                                int judge_time,
                                                int input_time) {
    if (!orig_score_state_apply_judgement_) return;

    (void)grade;
    (void)timing;
    constexpr int kJG_PERFECT_PURE = 0;
    constexpr int kJT_NONE = 0;
    orig_score_state_apply_judgement_(score, note, kJG_PERFECT_PURE, kJT_NONE, judge_time, input_time);
}

void AutoplayEngine::OnScoreStateApplyMiss(uintptr_t score, uintptr_t note) {
    if (!orig_score_state_apply_miss_) return;
    if (!addr_score_state_apply_judgement_) {
        orig_score_state_apply_miss_(score, note);
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

    if (!NoteCanApplyJudgement(logic_note, judge_time, kInputTimeSynth)) {
        orig_score_state_apply_miss_(score, note);
        return;
    }

    auto apply_judgement = orig_score_state_apply_judgement_
                               ? orig_score_state_apply_judgement_
                               : reinterpret_cast<ScoreStateApplyJudgementFn>(addr_score_state_apply_judgement_);
    apply_judgement(score, note, kJG_PERFECT_PURE, kJT_NONE, judge_time, kInputTimeSynth);
}

void AutoplayEngine::OnShowJudgementEffectAtNote(uintptr_t ctx,
                                                 uintptr_t note,
                                                 uintptr_t pos_ptr,
                                                 unsigned int kind,
                                                 int early_late) {
    if (!orig_show_judgement_effect_at_note_) return;
    (void)note;
    (void)pos_ptr;

    kind = 0;
    early_late = 0;
    orig_show_judgement_effect_at_note_(ctx, note, pos_ptr, kind, early_late);
}

int64_t AutoplayEngine::OnNoteEffectOnMiss(uintptr_t self, uintptr_t note) {
    if (note_effect_on_judgement_) {
        return note_effect_on_judgement_(self, note, 0, 0);
    }
    return orig_note_effect_on_miss_ ? orig_note_effect_on_miss_(self, note) : 0;
}

void AutoplayEngine::GameplayProcessLogicNotesHook(uintptr_t gameplay, uintptr_t play_scene_or_ctx) {
    Instance().OnGameplayProcessLogicNotes(game::Gameplay(gameplay), play_scene_or_ctx);
}

void AutoplayEngine::GameplayTryTapJudgementForTouchHook(uintptr_t gameplay, uintptr_t touch, int lane_hint) {
    Instance().OnGameplayTryTapJudgementForTouch(game::Gameplay(gameplay), game::TouchLike(touch), lane_hint);
}

void AutoplayEngine::ScoreStateApplyJudgementHook(uintptr_t score,
                                                  uintptr_t note,
                                                  int grade,
                                                  int timing,
                                                  int judge_time,
                                                  int input_time) {
    Instance().OnScoreStateApplyJudgement(score, note, grade, timing, judge_time, input_time);
}

void AutoplayEngine::ScoreStateApplyMissHook(uintptr_t score, uintptr_t note) {
    Instance().OnScoreStateApplyMiss(score, note);
}

void AutoplayEngine::ShowJudgementEffectAtNoteHook(uintptr_t ctx,
                                                   uintptr_t note,
                                                   uintptr_t pos_ptr,
                                                   unsigned int kind,
                                                   int early_late) {
    Instance().OnShowJudgementEffectAtNote(ctx, note, pos_ptr, kind, early_late);
}

int64_t AutoplayEngine::NoteEffectOnMissHook(uintptr_t self, uintptr_t note) {
    return Instance().OnNoteEffectOnMiss(self, note);
}

void AutoplayEngine::TryInstallAutoplayHooks() {
    if (!lib_base_) return;

    auto resolve = [&](uintptr_t &addr,
                       uintptr_t hint_offset,
                       const std::array<uint8_t, 16> &sig,
                       const char *name) {
        return ResolveAddr(addr, hint_offset, sig, name);
    };

    auto install_inline_hook = [&](uintptr_t &addr,
                                   uintptr_t hint_offset,
                                   const std::array<uint8_t, 16> &sig,
                                   auto hook_fn,
                                   auto &orig_fn,
                                   const char *name) {
        if (!resolve(addr, hint_offset, sig, name)) return;
        if (orig_fn) return;

        void *orig = nullptr;
        if (mem::InlineHook::InstallA64(addr, reinterpret_cast<void *>(hook_fn), &orig)) {
            using Fn = std::remove_reference_t<decltype(orig_fn)>;
            orig_fn = reinterpret_cast<Fn>(orig);
            ARC_LOGI("Hooked %s @ %p", name, reinterpret_cast<void *>(addr));
        } else {
            ARC_LOGE("Failed to hook %s @ %p", name, reinterpret_cast<void *>(addr));
        }
    };

    auto resolve_fnptr = [&](uintptr_t &addr,
                             uintptr_t hint_offset,
                             const std::array<uint8_t, 16> &sig,
                             auto &fn_out,
                             const char *name) {
        if (!resolve(addr, hint_offset, sig, name)) return;
        if (fn_out) return;
        using Fn = std::remove_reference_t<decltype(fn_out)>;
        fn_out = reinterpret_cast<Fn>(addr);
        ARC_LOGI("Resolved %s @ %p", name, reinterpret_cast<void *>(addr));
    };

    if (!patched_logicnote_miss_offsets_) {
        const uintptr_t p64a = lib_base_ + cfg::kLibcocos2dcpp_Patch_ProcessLogicNotes_add64_a;
        const uintptr_t p64b = lib_base_ + cfg::kLibcocos2dcpp_Patch_ProcessLogicNotes_add64_b;
        const uintptr_t pc8 = lib_base_ + cfg::kLibcocos2dcpp_Patch_ProcessLogicNotes_addC8;
        const bool ok = mem::Patcher::PatchA64ClearImm12(p64a, 0x11019148) &&
                        mem::Patcher::PatchA64ClearImm12(p64b, 0x11019148) &&
                        mem::Patcher::PatchA64ClearImm12(pc8, 0x11032148);
        patched_logicnote_miss_offsets_ = ok;
        ARC_LOGI("LogicNotes miss offsets patch: %s", ok ? "OK" : "FAILED");
    }

    install_inline_hook(addr_score_state_apply_judgement_,
                        cfg::kLibcocos2dcpp_ScoreState_applyJudgement,
                        cfg::kSig_ScoreState_applyJudgement,
                        ScoreStateApplyJudgementHook,
                        orig_score_state_apply_judgement_,
                        "ScoreState_applyJudgement");

    install_inline_hook(addr_score_state_apply_miss_,
                        cfg::kLibcocos2dcpp_ScoreState_applyMiss,
                        cfg::kSig_ScoreState_applyMiss,
                        ScoreStateApplyMissHook,
                        orig_score_state_apply_miss_,
                        "ScoreState_applyMiss");

    install_inline_hook(addr_gameplay_process_logic_notes_,
                        cfg::kLibcocos2dcpp_Gameplay_processLogicNotes,
                        cfg::kSig_Gameplay_processLogicNotes,
                        GameplayProcessLogicNotesHook,
                        orig_gameplay_process_logic_notes_,
                        "Gameplay_processLogicNotes");

    install_inline_hook(addr_show_judgement_effect_at_note_,
                        cfg::kLibcocos2dcpp_ShowJudgementEffectAtNote,
                        cfg::kSig_ShowJudgementEffectAtNote,
                        ShowJudgementEffectAtNoteHook,
                        orig_show_judgement_effect_at_note_,
                        "ShowJudgementEffectAtNote");

    install_inline_hook(addr_gameplay_try_tap_judgement_for_touch_,
                        cfg::kLibcocos2dcpp_Gameplay_tryTapJudgementForTouch,
                        cfg::kSig_Gameplay_tryTapJudgementForTouch,
                        GameplayTryTapJudgementForTouchHook,
                        orig_gameplay_try_tap_judgement_for_touch_,
                        "Gameplay_tryTapJudgementForTouch");

    if (!patched_logiccolor_accepts_touch_ &&
        resolve(addr_logic_color_accepts_touch_,
                cfg::kLibcocos2dcpp_LogicColor_acceptsTouch,
                cfg::kSig_LogicColor_acceptsTouch,
                "LogicColor_acceptsTouch")) {
        const bool ok =
            mem::Patcher::PatchU32WithPerms(addr_logic_color_accepts_touch_ + 0, 0x52800020) &&
            mem::Patcher::PatchU32WithPerms(addr_logic_color_accepts_touch_ + 4, 0xD65F03C0);
        patched_logiccolor_accepts_touch_ = ok;
        ARC_LOGI("Patched LogicColor_acceptsTouch @ %p: %s",
                 reinterpret_cast<void *>(addr_logic_color_accepts_touch_),
                 ok ? "OK" : "FAILED");
    }

    resolve_fnptr(addr_note_effect_on_judgement_,
                  cfg::kLibcocos2dcpp_NoteEffect_onJudgement,
                  cfg::kSig_NoteEffect_onJudgement,
                  note_effect_on_judgement_,
                  "NoteEffect_onJudgement");

    install_inline_hook(addr_note_effect_on_miss_,
                        cfg::kLibcocos2dcpp_NoteEffect_onMiss,
                        cfg::kSig_NoteEffect_onMiss,
                        NoteEffectOnMissHook,
                        orig_note_effect_on_miss_,
                        "NoteEffect_onMiss");
}

} // namespace arc_autoplay
