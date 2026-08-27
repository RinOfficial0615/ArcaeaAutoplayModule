#include "features/Autoplay.hpp"

#include "config/ModuleConfig.h"
#include "utils/Log.h"

namespace arc_helper {

Autoplay &Autoplay::Instance() {
    static Autoplay instance;
    return instance;
}

Autoplay::Autoplay() : Feature("Autoplay") {}

void Autoplay::Install(const cfg::GameProfile &profile) {
    if (hook_setup_done_) return;
    if (!enabled_) {
        hook_setup_done_ = true;
        return;
    }
    if (!profile.capabilities.autoplay) {
        ARC_LOGE("Capability unavailable for %s", profile.version_name);
        return;
    }
    cfg::SetRuntimeLayoutVersion(profile.id);
    auto &hook_manager = HookManager::Instance();
    if (!hook_manager.EnsureReady()) return;
    hook_manager_ = &hook_manager;
    lib_base_ = hook_manager.GetLibBase();
    if (!lib_base_) return;
    profile_ = &profile;
    TryInstallHooks(profile);
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
    const uintptr_t want = note.Addr();
    if (!want) return -1;
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role == TouchRole::None) continue;
        if (touches_[i].note.Addr() == want) return i;
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
    const uintptr_t fn = note.Vcall(cfg::autoplay::kLogicNote_vcall_setBeingTouched_off);
    if (!fn || !mem::IsAddrInLibraryExec(fn, cfg::module::kLibName)) return;

    using Fn = void (*)(uintptr_t, uintptr_t, int);
    reinterpret_cast<Fn>(fn)(note.Addr(), touch_like, now_ms);
}

bool Autoplay::ArcHasValidPlaySceneVcall(game::LogicArcNote arc_note) const {
    if (!arc_note) return false;

    const uintptr_t ctx = arc_note.PlaySceneOrCtx();
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

    const auto *profile = profile_;
    if (!profile) return;

    InitTouchesIfNeeded();

    // This build only keeps arc assignments in the touch pool.
    // Holds use a dedicated stub (`hold_touch_stub_`) and are not tracked.
    for (int i = 0; i < cfg::autoplay::kMaxSynthTouches; ++i) {
        if (touches_[i].role != TouchRole::Arc) {
            ReleaseTouch(touches_[i]);
        }
    }

    auto *note_begin = gameplay.PendingNoteBegin();
    auto *note_end = gameplay.PendingNoteEnd();
    const uintptr_t note_begin_addr = reinterpret_cast<uintptr_t>(note_begin);
    const uintptr_t note_end_addr = reinterpret_cast<uintptr_t>(note_end);
    if (!note_begin || !note_end || note_end_addr < note_begin_addr ||
        (note_end_addr - note_begin_addr) % sizeof(uintptr_t) != 0 ||
        (note_end_addr - note_begin_addr) / sizeof(uintptr_t) > 65536 ||
        !mem::ProcMaps::IsReadable(note_begin_addr, note_end_addr - note_begin_addr)) {
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

    std::array<uint8_t, cfg::autoplay::kMaxSynthTouches> arc_seen{};

    for (auto p = note_begin; p != note_end; ++p) {
        game::LogicNote note(*p);
        if (!note.Active()) continue;

        const uintptr_t typeinfo = note.Typeinfo();

        // Per-frame long-note touch flag reset.
        // The game uses this state to decide whether a long note is currently held.
        if (typeinfo == ti_arc || typeinfo == ti_hold) {
            note.clearLongTouchState();
        }

        if (typeinfo == ti_hold) {
            // Holds: mark head activated while within (t0, t1) and keep it touched.
            game::LogicHoldNote hold(note.Addr());
            const int t0 = hold.TimeStart();
            const int t1 = hold.TimeEnd();
            if (now_ms > t0 && now_ms < t1) {
                hold.SetHeadActivated(1);
                CallNoteSetBeingTouched(hold, hold_touch_stub_.Addr(), now_ms);
            }
            continue;
        }

        if (typeinfo != ti_arc) continue;

        game::LogicArcNote arc(note.Addr());
        const int t0 = arc.TimeStart();
        const int t1 = arc.TimeEnd();

        // Arcs: ignore void arcs and arcs that are inactive.
        if (arc.IsVoid()) continue;
        if (!arc.ActiveNow()) continue;
        const int64_t now = now_ms;
        if (now < static_cast<int64_t>(t0) - long_start_lead_ms_) continue;
        if (now > static_cast<int64_t>(t1) + long_end_lag_ms_) continue;

        int idx = FindTouchByNote(arc);
        if (idx < 0) {
            // Allocate a synthetic finger for this arc.
            idx = FindFreeTouchIndex();
            if (idx < 0) continue;
            touches_[idx].note = arc;
            touches_[idx].role = TouchRole::Arc;
        }

        const auto pos = arc.Pos();
        if (!pos) continue;
        const float x_norm = pos.XNorm();

        // Arcaea stores x as a normalized [0..1] value over the track width.
        // Convert that to world-space, then to NDC.
        const float x_base_world = x_norm * (2.0f * cfg::autoplay::kTrackHalfWidth) - cfg::autoplay::kTrackHalfWidth;
        const float x_world = x_base_world + arc.RuntimeX();
        const float y_world = arc.RuntimeY();

        touches_[idx].x = WorldXToNdc(x_world);
        touches_[idx].y = WorldYToNdc(y_world);
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

        if (touch.note.IsVoid() || !touch.note.ActiveNow()) {
            ReleaseTouch(touch);
            continue;
        }

        if (!ArcHasValidPlaySceneVcall(touch.note)) {
            ReleaseTouch(touch);
            continue;
        }

        WriteTouchStub(i, now_ms);
        CallNoteSetBeingTouched(touch.note, touch_stubs_[i].Addr(), now_ms);
    }
}

bool Autoplay::NoteCanApplyJudgement(game::LogicNote note, int judge_time, int input_time) const {
    if (!note) return false;

    const uintptr_t gate_fn = note.Vcall(cfg::autoplay::kLogicNote_vcall_canApplyJudgement_off);
    if (!gate_fn || !mem::IsAddrInLibraryExec(gate_fn, cfg::module::kLibName)) return false;

    using GateFn = bool (*)(uintptr_t, int, int);
    return reinterpret_cast<GateFn>(gate_fn)(note.Addr(), judge_time, input_time);
}

void Autoplay::OnGameplayProcessLogicNotes(game::Gameplay gameplay, uintptr_t play_scene_or_ctx) {
    // This hook runs every logic tick and is our main timebase.
    if (!gameplay) return;
    const int now_ms = gameplay.GetTimer().NowMs();
    last_now_ms_ = now_ms;

    if (enabled_) AutoplayLongNotesTick(gameplay, now_ms);

    CALL_ORIG(GameplayProcessLogicNotesHook, gameplay.Addr(), play_scene_or_ctx);
}

void Autoplay::OnGameplayTryTapJudgementForTouch(game::Gameplay gameplay, game::TouchLike touch, int lane_hint) const {
    if (!touch) return;
    // Synthetic touches use the same game path as real touches. Forwarding
    // real input is essential when Autoplay is enabled alongside manual play.
    CALL_ORIG(GameplayTryTapJudgementForTouchHook, gameplay.Addr(), touch.Addr(), lane_hint);
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
        judge_time = logic_note.TimeStart();
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

    std::array<HookManager::InlineHookRegistration, 6> registrations = {
        hook_manager_->RegisterInlineHook(addr_gameplay_process_logic_notes_,
                                         offsets.gameplay_process_logic_notes,
                                         cfg::autoplay::kSig_Gameplay_processLogicNotes,
                                         GameplayProcessLogicNotesHook,
                                         "Gameplay_processLogicNotes"),
        hook_manager_->RegisterInlineHook(addr_score_state_apply_judgement_,
                                         offsets.score_state_apply_judgement,
                                         cfg::autoplay::ScoreStateApplyJudgementSignature(profile.id),
                                         ScoreStateApplyJudgementHook,
                                         "ScoreState_applyJudgement"),
        hook_manager_->RegisterInlineHook(addr_score_state_apply_miss_,
                                         offsets.score_state_apply_miss,
                                         cfg::autoplay::ScoreStateApplyMissSignature(profile.id),
                                         ScoreStateApplyMissHook,
                                         "ScoreState_applyMiss"),
        hook_manager_->RegisterInlineHook(addr_show_judgement_effect_at_note_,
                                         offsets.show_judgement_effect_at_note,
                                         cfg::autoplay::kSig_ShowJudgementEffectAtNote,
                                         ShowJudgementEffectAtNoteHook,
                                         "ShowJudgementEffectAtNote"),
        hook_manager_->RegisterInlineHook(addr_gameplay_try_tap_judgement_for_touch_,
                                         offsets.gameplay_try_tap_judgement_for_touch,
                                         cfg::autoplay::kSig_Gameplay_tryTapJudgementForTouch,
                                         GameplayTryTapJudgementForTouchHook,
                                         "Gameplay_tryTapJudgementForTouch"),
        hook_manager_->RegisterInlineHook(addr_note_effect_on_miss_,
                                         offsets.note_effect_on_miss,
                                         cfg::autoplay::kSig_NoteEffect_onMiss,
                                         NoteEffectOnMissHook,
                                         "NoteEffect_onMiss"),
    };
    if (std::ranges::any_of(registrations, [](const auto &registration) {
            return !registration;
        })) {
        ARC_LOGE("Inline hook registration failed");
        return;
    }

    if (!hook_manager_->ResolveAddress(addr_logic_color_accepts_touch_,
                                       offsets.logic_color_accepts_touch,
                                       cfg::autoplay::kSig_LogicColor_acceptsTouch,
                                       "LogicColor_acceptsTouch")) {
        ARC_LOGE("Failed to resolve LogicColor_acceptsTouch");
        return;
    }

    hook_manager_->ResolveFunctionPtr(addr_note_effect_on_judgement_,
                                     offsets.note_effect_on_judgement,
                                     cfg::autoplay::kSig_NoteEffect_onJudgement,
                                     note_effect_on_judgement_,
                                     "NoteEffect_onJudgement");

    if (!patch_transaction_.IsApplied()) {
        const uintptr_t p64a = lib_base_ + offsets.patch_process_logic_notes_add64_a;
        const uintptr_t p64b = lib_base_ + offsets.patch_process_logic_notes_add64_b;
        const uintptr_t pc8 = lib_base_ + offsets.patch_process_logic_notes_addc8;
        const auto current_a = mem::RuntimeMemory::Process().Read<uint32_t>(p64a);
        const auto current_b = mem::RuntimeMemory::Process().Read<uint32_t>(p64b);
        const auto current_c = mem::RuntimeMemory::Process().Read<uint32_t>(pc8);
        if (!current_a || !current_b || !current_c ||
            *current_a != cfg::autoplay::kPatchExpectedAdd64A ||
            *current_b != cfg::autoplay::kPatchExpectedAdd64B ||
            *current_c != cfg::autoplay::kPatchExpectedAddC8) {
            ARC_LOGE("Logic-note patch signature mismatch");
            return;
        }

        std::array<std::byte, sizeof(uint32_t)> expected_a{};
        std::array<std::byte, sizeof(uint32_t)> expected_b{};
        std::array<std::byte, sizeof(uint32_t)> expected_c{};
        std::array<std::byte, sizeof(uint32_t)> replacement_a{};
        std::array<std::byte, sizeof(uint32_t)> replacement_b{};
        std::array<std::byte, sizeof(uint32_t)> replacement_c{};
        std::memcpy(expected_a.data(), &*current_a, expected_a.size());
        std::memcpy(expected_b.data(), &*current_b, expected_b.size());
        std::memcpy(expected_c.data(), &*current_c, expected_c.size());
        const uint32_t patched_a = *current_a & ~cfg::autoplay::kPatchImmediateMask;
        const uint32_t patched_b = *current_b & ~cfg::autoplay::kPatchImmediateMask;
        const uint32_t patched_c = *current_c & ~cfg::autoplay::kPatchImmediateMask;
        std::memcpy(replacement_a.data(), &patched_a, replacement_a.size());
        std::memcpy(replacement_b.data(), &patched_b, replacement_b.size());
        std::memcpy(replacement_c.data(), &patched_c, replacement_c.size());

        std::array<std::byte, 8> expected_color{};
        if (auto read = mem::RuntimeMemory::Process().ReadBytes(
                addr_logic_color_accepts_touch_, std::span<std::byte>(expected_color));
            !read) {
            ARC_LOGE("Failed to read LogicColor_acceptsTouch");
            return;
        }
        constexpr std::array<uint32_t, 2> color_replacement =
            cfg::autoplay::kPatchColorGateReplacement;
        std::array<std::byte, 8> replacement_color{};
        std::memcpy(replacement_color.data(), color_replacement.data(), replacement_color.size());

        const std::array<mem::PatchDescriptor, 4> patches = {{
            {p64a, std::span<const std::byte>(expected_a), std::span<const std::byte>(replacement_a)},
            {p64b, std::span<const std::byte>(expected_b), std::span<const std::byte>(replacement_b)},
            {pc8, std::span<const std::byte>(expected_c), std::span<const std::byte>(replacement_c)},
            {addr_logic_color_accepts_touch_,
             std::span<const std::byte>(expected_color),
             std::span<const std::byte>(replacement_color)},
        }};
        if (!patch_transaction_.Apply(patches)) {
            if (patch_transaction_.IsDegraded() && !patch_transaction_.Rollback()) {
                ARC_LOGE("Degraded patch rollback failed");
            }
            ARC_LOGE("Patch transaction failed");
            return;
        }
        patched_logicnote_miss_offsets_ = true;
        patched_logiccolor_accepts_touch_ = true;
        ARC_LOGI("Logic-note and touch patches committed");
    }

    if (!patched_logicnote_miss_offsets_ || !patched_logiccolor_accepts_touch_ ||
        !note_effect_on_judgement_ ||
        !hook_manager_->CommitInlineHook(std::span<HookManager::InlineHookRegistration>(registrations))) {
        ARC_LOGE("Setup transaction failed");
        if (patch_transaction_.IsApplied()) {
            (void)patch_transaction_.Rollback();
            patched_logicnote_miss_offsets_ = false;
            patched_logiccolor_accepts_touch_ = false;
        }
        return;
    }

    const bool hooks_ready =
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(ScoreStateApplyJudgementHook)) &&
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(ScoreStateApplyMissHook)) &&
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(GameplayProcessLogicNotesHook)) &&
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(ShowJudgementEffectAtNoteHook)) &&
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(GameplayTryTapJudgementForTouchHook)) &&
        hook_manager_->HasOriginalForHook(reinterpret_cast<void *>(NoteEffectOnMissHook));

    hook_setup_done_ = hooks_ready &&
                       patched_logicnote_miss_offsets_ &&
                       patched_logiccolor_accepts_touch_ &&
                       note_effect_on_judgement_ != nullptr;
}

} // namespace arc_helper
