#pragma once

#include <cstddef>
#include <cstdint>

#include "game/GameProfile.hpp"

namespace arc_helper::cfg::layouts {

// ============================================================================
//  Mirror structs of in-game C++ objects (on-heap, not constructed by us).
//
//  Each struct describes the memory layout at a known address.  Named fields
//  mark offsets whose purpose we have confirmed; unknown regions are explicit
//  uint8_t pad_XX[...] arrays so that sizeof() + offsetof() stay correct.
//
//  Version handling:
//    Every struct is templated on GameVersionId.  The fields used here are
//    shared through 6.16.8c (verified by cross-version function/BL comparison).
//    7.0.x inserts one pointer-sized member into the note-family derived area,
//    shifting every derived-class member after it by +8; the base-zone fields
//    (timeStart/timeEnd, pos_ptr, playSceneCtx, active, Timer, Gameplay ranges)
//    are verified unchanged.  Only the affected specializations are overridden.
// ============================================================================

// ---------------------------------------------------------------------------
//  Timer  (accessed via gameplay->timer)
// ---------------------------------------------------------------------------
namespace impl {
struct TimerBase {
    uint8_t  pad_00[0x20];
    int32_t  msA;                       // +0x20  (32)
    uint8_t  pad_24[4];
    int32_t  msB;                       // +0x28  (40)
    uint8_t  pad_2C[1];
    uint8_t  flag;                      // +0x2D  (45)   is-audio-timer flag
    uint8_t  pad_2E[6];
    int32_t  msC;                       // +0x34  (52)
};
} // namespace impl

template <GameVersionId Ver>
struct Timer : impl::TimerBase {};

// ---------------------------------------------------------------------------
//  Gameplay
// ---------------------------------------------------------------------------
namespace impl {
struct GameplayBase {
    uint8_t   pad_00[0x30];
    uintptr_t timer;                    // +0x30  (48)
    uint8_t   pad_38[0x68];
    uintptr_t note_begin;               // +0xA0  (160)
    uintptr_t note_end;                 // +0xA8  (168)
};
} // namespace impl

template <GameVersionId Ver>
struct Gameplay : impl::GameplayBase {};

// ---------------------------------------------------------------------------
//  LogicNote (base of tap / hold / arc / flick / arctap)
// ---------------------------------------------------------------------------
namespace impl {
struct NoteBase {
    uint8_t   pad_00[0x18];
    int32_t   timeStart;                // +0x18  (24)
    int32_t   timeEnd;                  // +0x1C  (28)
    uintptr_t pos_ptr;                  // +0x20  (32)   -> NotePosition
    uint8_t   pad_28[0x18];
    uintptr_t playSceneCtx;             // +0x40  (64)
    uint8_t   pad_48[0x0C];
    uint8_t   active;                   // +0x54  (84)
    uint8_t   pad_55[0x07];
    uint16_t  longTouchState;           // +0x5C  (92)
    uint8_t   pad_5E[0x6E];
    float     runtimeX;                 // +0xCC  (204)
    float     runtimeY;                 // +0xD0  (208)
};

// 7.0.x: identical to NoteBase except the arc runtime float pair moved into
// the version-specialized NoteRuntimePos; taps never access it, so it is
// dropped here.
struct Note7000cBase {
    uint8_t   pad_00[0x18];
    int32_t   timeStart;                // +0x18  (24)
    int32_t   timeEnd;                  // +0x1C  (28)
    uintptr_t pos_ptr;                  // +0x20  (32)   -> NotePosition
    uint8_t   pad_28[0x18];
    uintptr_t playSceneCtx;             // +0x40  (64)
    uint8_t   pad_48[0x0C];
    uint8_t   active;                   // +0x54  (84)
    uint8_t   pad_55[0x07];
    uint16_t  longTouchState;           // +0x5C  (92)
    uint8_t   pad_5E[0x6E];             // tail ends at 0xCC
};
} // namespace impl

template <GameVersionId Ver>
struct Note : impl::NoteBase {};

template <>
struct Note<GameVersionId::k7000c> : impl::Note7000cBase {};

template <>
struct Note<GameVersionId::k7001c> : impl::Note7000cBase {};

template <>
struct Note<GameVersionId::k70255c> : impl::Note7000cBase {};

// ---------------------------------------------------------------------------
//  NotePosition
// ---------------------------------------------------------------------------
namespace impl {
struct NotePositionBase {
    uint8_t pad_00[0x14];
    float   xNorm;                      // +0x14  (20)
};
} // namespace impl

template <GameVersionId Ver>
struct NotePosition : impl::NotePositionBase {};

// ---------------------------------------------------------------------------
//  LogicArcNote (same note family as LogicNote; overlay over its tail)
// ---------------------------------------------------------------------------
namespace impl {
struct ArcNoteBase {
    uint8_t  pad_00[0x9C];
    int32_t  isVoid;                    // +0x9C  (156)
    uint8_t  pad_A0[0x28];
    uint8_t  activeNow;                 // +0xC8  (200)
};

// 7.0.x: one inserted member before isVoid shifts the derived tail by +8.
struct ArcNote7000cBase {
    uint8_t  pad_00[0xA4];
    int32_t  isVoid;                    // +0xA4  (164)
    uint8_t  pad_A8[0x28];
    uint8_t  activeNow;                 // +0xD0  (208)
};
} // namespace impl

template <GameVersionId Ver>
struct ArcNote : impl::ArcNoteBase {};

template <>
struct ArcNote<GameVersionId::k7000c> : impl::ArcNote7000cBase {};

template <>
struct ArcNote<GameVersionId::k7001c> : impl::ArcNote7000cBase {};

template <>
struct ArcNote<GameVersionId::k70255c> : impl::ArcNote7000cBase {};

// ---------------------------------------------------------------------------
//  LogicNote runtime position overlay (arc objects only).  The game writes the
//  current track-space position per arc into this float pair; taps never touch
//  it, so only arcs observe the 7.0.x shift.
// ---------------------------------------------------------------------------
namespace impl {
struct NoteRuntimePosBase {
    uint8_t pad_00[0xCC];
    float   runtimeX;                   // +0xCC  (204)
    float   runtimeY;                   // +0xD0  (208)
};

// 7.0.x: same +8 shift as the other derived members.
struct NoteRuntimePos7000cBase {
    uint8_t pad_00[0xD4];
    float   runtimeX;                   // +0xD4  (212)
    float   runtimeY;                   // +0xD8  (216)
};
} // namespace impl

template <GameVersionId Ver>
struct NoteRuntimePos : impl::NoteRuntimePosBase {};

template <>
struct NoteRuntimePos<GameVersionId::k7000c> : impl::NoteRuntimePos7000cBase {};

template <>
struct NoteRuntimePos<GameVersionId::k7001c> : impl::NoteRuntimePos7000cBase {};

template <>
struct NoteRuntimePos<GameVersionId::k70255c> : impl::NoteRuntimePos7000cBase {};

// ---------------------------------------------------------------------------
//  LogicHoldNote (extends LogicNote)
// ---------------------------------------------------------------------------
namespace impl {
struct HoldNoteBase {
    uint8_t pad_00[0xA0];
    uint8_t headActivated;              // +0xA0  (160)
};

// 7.0.x: shifted by the same derived-area insertion.
struct HoldNote7000cBase {
    uint8_t pad_00[0xA8];
    uint8_t headActivated;              // +0xA8  (168)
};
} // namespace impl

template <GameVersionId Ver>
struct HoldNote : impl::HoldNoteBase {};

template <>
struct HoldNote<GameVersionId::k7000c> : impl::HoldNote7000cBase {};

template <>
struct HoldNote<GameVersionId::k7001c> : impl::HoldNote7000cBase {};

template <>
struct HoldNote<GameVersionId::k70255c> : impl::HoldNote7000cBase {};

// ---------------------------------------------------------------------------
//  TouchLike  (synthetic or hardware touch passed to judgement calls)
// ---------------------------------------------------------------------------
namespace impl {
struct TouchLikeBase {
    uint8_t  pad_00[0x0C];
    int32_t  sysId;                     // +0x0C  (12)
    uint8_t  pad_10[0x0C];
    float    ndcX;                      // +0x1C  (28)
    float    ndcY;                      // +0x20  (32)
    uint8_t  pad_24[0x10];
    int32_t  touchUid;                  // +0x34  (52)
    uint8_t  pad_38[0x04];
    int32_t  timestamp;                 // +0x3C  (60)
};
} // namespace impl

template <GameVersionId Ver>
struct TouchLike : impl::TouchLikeBase {};

// ---------------------------------------------------------------------------
//  HttpRequest   (cocos2d-x network thread)
// ---------------------------------------------------------------------------
namespace impl {
struct HttpRequestBase {
    uint8_t   pad_00[0x0C];
    uint32_t  type;                     // +0x0C  (12)   GET=0,POST=1,PUT=2,DELETE=3
    uint8_t   pad_10[0x18];
    uintptr_t bodyBegin;                // +0x28  (40)
    uintptr_t bodyEnd;                  // +0x30  (48)
};
} // namespace impl

template <GameVersionId Ver>
struct HttpRequest : impl::HttpRequestBase {};

// ---------------------------------------------------------------------------
//  HttpResponse  (cocos2d-x network thread)
// ---------------------------------------------------------------------------
namespace impl {
struct HttpResponseBase {
    uint8_t   pad_00[0x10];
    uintptr_t request;                  // +0x10  (16)   -> HttpRequest
    uint8_t   succeed;                  // +0x18  (24)
    uint8_t   pad_19[0x07];
    uintptr_t bodyVec;                  // +0x20  (32)   std::vector<char>
    uint8_t   pad_28[0x28];
    int64_t   statusCode;               // +0x50  (80)
};
} // namespace impl

template <GameVersionId Ver>
struct HttpResponse : impl::HttpResponseBase {};

// ---------------------------------------------------------------------------
//  Song / difficulty / registry owner  (custom-chart runtime, 6.16.2c)
// ---------------------------------------------------------------------------
inline constexpr size_t kSongDifficultySlotCount = 5;

namespace impl {
struct SongBase {
    uint8_t   pad_00[0x1C0];
    uint8_t   remote_pack;              // +0x1C0  chartPath / play dialog / preview dl_ prefix
    uint8_t   pad_1C1[0x67];
    uintptr_t difficulty_pointers[kSongDifficultySlotCount]; // +0x228
    uint8_t   difficulty_presence[kSongDifficultySlotCount]; // +0x250
};

struct SongDifficultyBase {
    uint8_t  pad_00[0xF0];
    uint8_t  lock;                      // +0xF0  local unlock / lock flag
    uint8_t  pad_F1[0x33];
    int32_t  rating_class;              // +0x124
};

struct SongRegistryOwnerBase {
    uint8_t   pad_00[0x20];
    uintptr_t registry;                 // +0x20
};
} // namespace impl

template <GameVersionId Ver>
struct Song : impl::SongBase {};

template <GameVersionId Ver>
struct SongDifficulty : impl::SongDifficultyBase {};

template <GameVersionId Ver>
struct SongRegistryOwner : impl::SongRegistryOwnerBase {};

// ---------------------------------------------------------------------------
//  Compile-time verification of every documented field offset.
// ---------------------------------------------------------------------------
namespace verify {
using V = GameVersionId;

static_assert(offsetof(Timer<V::k61211c>,      msA)            == 32);
static_assert(offsetof(Timer<V::k61211c>,      msB)            == 40);
static_assert(offsetof(Timer<V::k61211c>,      flag)           == 45);
static_assert(offsetof(Timer<V::k61211c>,      msC)            == 52);

static_assert(offsetof(Gameplay<V::k61211c>,   timer)          == 48);
static_assert(offsetof(Gameplay<V::k61211c>,   note_begin)     == 160);
static_assert(offsetof(Gameplay<V::k61211c>,   note_end)       == 168);

static_assert(offsetof(Note<V::k61211c>,       timeStart)      == 24);
static_assert(offsetof(Note<V::k61211c>,       timeEnd)        == 28);
static_assert(offsetof(Note<V::k61211c>,       pos_ptr)        == 32);
static_assert(offsetof(Note<V::k61211c>,       playSceneCtx)   == 64);
static_assert(offsetof(Note<V::k61211c>,       active)         == 84);
static_assert(offsetof(Note<V::k61211c>,       longTouchState) == 92);
static_assert(offsetof(Note<V::k61211c>,       runtimeX)       == 204);
static_assert(offsetof(Note<V::k61211c>,       runtimeY)       == 208);

static_assert(offsetof(NotePosition<V::k61211c>, xNorm)        == 20);

static_assert(offsetof(ArcNote<V::k61211c>,    isVoid)         == 156);
static_assert(offsetof(ArcNote<V::k61211c>,    activeNow)      == 200);

static_assert(offsetof(HoldNote<V::k61211c>,   headActivated)  == 160);

static_assert(offsetof(TouchLike<V::k61211c>,  sysId)          == 12);
static_assert(offsetof(TouchLike<V::k61211c>,  ndcX)           == 28);
static_assert(offsetof(TouchLike<V::k61211c>,  ndcY)           == 32);
static_assert(offsetof(TouchLike<V::k61211c>,  touchUid)       == 52);
static_assert(offsetof(TouchLike<V::k61211c>,  timestamp)      == 60);

static_assert(offsetof(HttpRequest<V::k61211c>,  type)         == 12);
static_assert(offsetof(HttpRequest<V::k61211c>,  bodyBegin)    == 40);
static_assert(offsetof(HttpRequest<V::k61211c>,  bodyEnd)      == 48);

static_assert(offsetof(HttpResponse<V::k61211c>, request)      == 16);
static_assert(offsetof(HttpResponse<V::k61211c>, succeed)      == 24);
static_assert(offsetof(HttpResponse<V::k61211c>, bodyVec)      == 32);
static_assert(offsetof(HttpResponse<V::k61211c>, statusCode)   == 80);

// 6.16.2c capability gate: every field consumed by autoplay/network must keep
// the verified shared layout before that profile can be armed.
static_assert(offsetof(Timer<V::k6162c>, msA) == 32);
static_assert(offsetof(Timer<V::k6162c>, msB) == 40);
static_assert(offsetof(Timer<V::k6162c>, flag) == 45);
static_assert(offsetof(Timer<V::k6162c>, msC) == 52);
static_assert(offsetof(Gameplay<V::k6162c>, timer) == 48);
static_assert(offsetof(Gameplay<V::k6162c>, note_begin) == 160);
static_assert(offsetof(Gameplay<V::k6162c>, note_end) == 168);
static_assert(offsetof(Note<V::k6162c>, timeStart) == 24);
static_assert(offsetof(Note<V::k6162c>, timeEnd) == 28);
static_assert(offsetof(Note<V::k6162c>, pos_ptr) == 32);
static_assert(offsetof(Note<V::k6162c>, playSceneCtx) == 64);
static_assert(offsetof(Note<V::k6162c>, active) == 84);
static_assert(offsetof(Note<V::k6162c>, longTouchState) == 92);
static_assert(offsetof(Note<V::k6162c>, runtimeX) == 204);
static_assert(offsetof(Note<V::k6162c>, runtimeY) == 208);
static_assert(offsetof(NotePosition<V::k6162c>, xNorm) == 20);
static_assert(offsetof(ArcNote<V::k6162c>, isVoid) == 156);
static_assert(offsetof(ArcNote<V::k6162c>, activeNow) == 200);
static_assert(offsetof(HoldNote<V::k6162c>, headActivated) == 160);
static_assert(offsetof(TouchLike<V::k6162c>, sysId) == 12);
static_assert(offsetof(TouchLike<V::k6162c>, ndcX) == 28);
static_assert(offsetof(HttpRequest<V::k6162c>, type) == 12);
static_assert(offsetof(HttpRequest<V::k6162c>, bodyBegin) == 40);
static_assert(offsetof(HttpRequest<V::k6162c>, bodyEnd) == 48);
static_assert(offsetof(HttpResponse<V::k6162c>, request) == 16);
static_assert(offsetof(HttpResponse<V::k6162c>, succeed) == 24);
static_assert(offsetof(HttpResponse<V::k6162c>, bodyVec) == 32);
static_assert(offsetof(HttpResponse<V::k6162c>, statusCode) == 80);

static_assert(offsetof(Song<V::k6162c>, remote_pack) == 0x1C0);
static_assert(offsetof(Song<V::k6162c>, difficulty_pointers) == 0x228);
static_assert(offsetof(Song<V::k6162c>, difficulty_presence) == 0x250);
static_assert(offsetof(Song<V::k6162c>, difficulty_presence) ==
              offsetof(Song<V::k6162c>, difficulty_pointers) +
                  kSongDifficultySlotCount * sizeof(uintptr_t));
static_assert(offsetof(SongDifficulty<V::k6162c>, lock) == 0xF0);
static_assert(offsetof(SongDifficulty<V::k6162c>, rating_class) == 0x124);
static_assert(sizeof(SongDifficulty<V::k6162c>) == 0x128);
static_assert(offsetof(SongRegistryOwner<V::k6162c>, registry) == 32);

// 6.16.8c: game logic is byte-identical to 6.16.2c (verified by cross-version
// function comparison), so every consumed layout offset must carry over.
static_assert(offsetof(Timer<V::k6168c>, msA) == 32);
static_assert(offsetof(Timer<V::k6168c>, msB) == 40);
static_assert(offsetof(Timer<V::k6168c>, flag) == 45);
static_assert(offsetof(Timer<V::k6168c>, msC) == 52);
static_assert(offsetof(Gameplay<V::k6168c>, timer) == 48);
static_assert(offsetof(Gameplay<V::k6168c>, note_begin) == 160);
static_assert(offsetof(Gameplay<V::k6168c>, note_end) == 168);
static_assert(offsetof(Note<V::k6168c>, timeStart) == 24);
static_assert(offsetof(Note<V::k6168c>, timeEnd) == 28);
static_assert(offsetof(Note<V::k6168c>, pos_ptr) == 32);
static_assert(offsetof(Note<V::k6168c>, playSceneCtx) == 64);
static_assert(offsetof(Note<V::k6168c>, active) == 84);
static_assert(offsetof(Note<V::k6168c>, longTouchState) == 92);
static_assert(offsetof(Note<V::k6168c>, runtimeX) == 204);
static_assert(offsetof(Note<V::k6168c>, runtimeY) == 208);
static_assert(offsetof(NotePosition<V::k6168c>, xNorm) == 20);
static_assert(offsetof(ArcNote<V::k6168c>, isVoid) == 156);
static_assert(offsetof(ArcNote<V::k6168c>, activeNow) == 200);
static_assert(offsetof(HoldNote<V::k6168c>, headActivated) == 160);
static_assert(offsetof(TouchLike<V::k6168c>, sysId) == 12);
static_assert(offsetof(TouchLike<V::k6168c>, ndcX) == 28);
static_assert(offsetof(HttpRequest<V::k6168c>, type) == 12);
static_assert(offsetof(HttpRequest<V::k6168c>, bodyBegin) == 40);
static_assert(offsetof(HttpRequest<V::k6168c>, bodyEnd) == 48);
static_assert(offsetof(HttpResponse<V::k6168c>, request) == 16);
static_assert(offsetof(HttpResponse<V::k6168c>, succeed) == 24);
static_assert(offsetof(HttpResponse<V::k6168c>, bodyVec) == 32);
static_assert(offsetof(HttpResponse<V::k6168c>, statusCode) == 80);

static_assert(offsetof(Song<V::k6168c>, remote_pack) == 0x1C0);
static_assert(offsetof(Song<V::k6168c>, difficulty_pointers) == 0x228);
static_assert(offsetof(Song<V::k6168c>, difficulty_presence) == 0x250);
static_assert(offsetof(SongDifficulty<V::k6168c>, lock) == 0xF0);
static_assert(offsetof(SongDifficulty<V::k6168c>, rating_class) == 0x124);
static_assert(sizeof(SongDifficulty<V::k6168c>) == 0x128);
static_assert(offsetof(SongRegistryOwner<V::k6168c>, registry) == 32);

// 7.0.0c: base-zone fields keep the shared layout (verified against the matched
// core functions); the note-family derived tail shifted by one pointer (see the
// impl::*7000c* bases above and docs/offsets/7.0.0c-offsets.md).
static_assert(offsetof(Timer<V::k7000c>, msA) == 32);
static_assert(offsetof(Timer<V::k7000c>, msB) == 40);
static_assert(offsetof(Timer<V::k7000c>, flag) == 45);
static_assert(offsetof(Timer<V::k7000c>, msC) == 52);
static_assert(offsetof(Gameplay<V::k7000c>, timer) == 48);
static_assert(offsetof(Gameplay<V::k7000c>, note_begin) == 160);
static_assert(offsetof(Gameplay<V::k7000c>, note_end) == 168);
static_assert(offsetof(Note<V::k7000c>, timeStart) == 24);
static_assert(offsetof(Note<V::k7000c>, timeEnd) == 28);
static_assert(offsetof(Note<V::k7000c>, pos_ptr) == 32);
static_assert(offsetof(Note<V::k7000c>, playSceneCtx) == 64);
static_assert(offsetof(Note<V::k7000c>, active) == 84);
static_assert(offsetof(Note<V::k7000c>, longTouchState) == 92);
static_assert(offsetof(NotePosition<V::k7000c>, xNorm) == 20);
static_assert(offsetof(ArcNote<V::k7000c>, isVoid) == 164);
static_assert(offsetof(ArcNote<V::k7000c>, activeNow) == 208);
static_assert(offsetof(NoteRuntimePos<V::k7000c>, runtimeX) == 212);
static_assert(offsetof(NoteRuntimePos<V::k7000c>, runtimeY) == 216);
static_assert(offsetof(HoldNote<V::k7000c>, headActivated) == 168);
static_assert(offsetof(TouchLike<V::k7000c>, sysId) == 12);
static_assert(offsetof(TouchLike<V::k7000c>, ndcX) == 28);
static_assert(offsetof(HttpRequest<V::k7000c>, type) == 12);
static_assert(offsetof(HttpRequest<V::k7000c>, bodyBegin) == 40);
static_assert(offsetof(HttpRequest<V::k7000c>, bodyEnd) == 48);
static_assert(offsetof(HttpResponse<V::k7000c>, request) == 16);
static_assert(offsetof(HttpResponse<V::k7000c>, succeed) == 24);
static_assert(offsetof(HttpResponse<V::k7000c>, bodyVec) == 32);
static_assert(offsetof(HttpResponse<V::k7000c>, statusCode) == 80);

static_assert(offsetof(Song<V::k7000c>, remote_pack) == 0x1C0);
static_assert(offsetof(Song<V::k7000c>, difficulty_pointers) == 0x228);
static_assert(offsetof(Song<V::k7000c>, difficulty_presence) == 0x250);
static_assert(offsetof(SongDifficulty<V::k7000c>, lock) == 0xF0);
static_assert(offsetof(SongDifficulty<V::k7000c>, rating_class) == 0x124);
static_assert(sizeof(SongDifficulty<V::k7000c>) == 0x128);
static_assert(offsetof(SongRegistryOwner<V::k7000c>, registry) == 32);

// 7.0.1c keeps the 7.0.0c object layouts. The paired gameplay, chart-path,
// play-launcher, and availability bodies retain every consumed displacement.
static_assert(offsetof(Timer<V::k7001c>, msA) == 32);
static_assert(offsetof(Timer<V::k7001c>, msB) == 40);
static_assert(offsetof(Timer<V::k7001c>, flag) == 45);
static_assert(offsetof(Timer<V::k7001c>, msC) == 52);
static_assert(offsetof(Gameplay<V::k7001c>, timer) == 48);
static_assert(offsetof(Gameplay<V::k7001c>, note_begin) == 160);
static_assert(offsetof(Gameplay<V::k7001c>, note_end) == 168);
static_assert(offsetof(Note<V::k7001c>, timeStart) == 24);
static_assert(offsetof(Note<V::k7001c>, timeEnd) == 28);
static_assert(offsetof(Note<V::k7001c>, pos_ptr) == 32);
static_assert(offsetof(Note<V::k7001c>, playSceneCtx) == 64);
static_assert(offsetof(Note<V::k7001c>, active) == 84);
static_assert(offsetof(Note<V::k7001c>, longTouchState) == 92);
static_assert(offsetof(NotePosition<V::k7001c>, xNorm) == 20);
static_assert(offsetof(ArcNote<V::k7001c>, isVoid) == 164);
static_assert(offsetof(ArcNote<V::k7001c>, activeNow) == 208);
static_assert(offsetof(NoteRuntimePos<V::k7001c>, runtimeX) == 212);
static_assert(offsetof(NoteRuntimePos<V::k7001c>, runtimeY) == 216);
static_assert(offsetof(HoldNote<V::k7001c>, headActivated) == 168);
static_assert(offsetof(TouchLike<V::k7001c>, sysId) == 12);
static_assert(offsetof(TouchLike<V::k7001c>, ndcX) == 28);
static_assert(offsetof(HttpRequest<V::k7001c>, type) == 12);
static_assert(offsetof(HttpRequest<V::k7001c>, bodyBegin) == 40);
static_assert(offsetof(HttpRequest<V::k7001c>, bodyEnd) == 48);
static_assert(offsetof(HttpResponse<V::k7001c>, request) == 16);
static_assert(offsetof(HttpResponse<V::k7001c>, succeed) == 24);
static_assert(offsetof(HttpResponse<V::k7001c>, bodyVec) == 32);
static_assert(offsetof(HttpResponse<V::k7001c>, statusCode) == 80);
static_assert(offsetof(Song<V::k7001c>, remote_pack) == 0x1C0);
static_assert(offsetof(Song<V::k7001c>, difficulty_pointers) == 0x228);
static_assert(offsetof(Song<V::k7001c>, difficulty_presence) == 0x250);
static_assert(offsetof(SongDifficulty<V::k7001c>, lock) == 0xF0);
static_assert(offsetof(SongDifficulty<V::k7001c>, rating_class) == 0x124);
static_assert(sizeof(SongDifficulty<V::k7001c>) == 0x128);
static_assert(offsetof(SongRegistryOwner<V::k7001c>, registry) == 32);

// 7.0.255c keeps the 7.0.0c/7.0.1c object layouts. The paired gameplay,
// chart-path, play-launcher, and availability bodies retain every consumed
// displacement (relocated with byte-identical extents; see
// docs/offsets/7.0.255c-offsets.md).
static_assert(offsetof(Timer<V::k70255c>, msA) == 32);
static_assert(offsetof(Timer<V::k70255c>, msB) == 40);
static_assert(offsetof(Timer<V::k70255c>, flag) == 45);
static_assert(offsetof(Timer<V::k70255c>, msC) == 52);
static_assert(offsetof(Gameplay<V::k70255c>, timer) == 48);
static_assert(offsetof(Gameplay<V::k70255c>, note_begin) == 160);
static_assert(offsetof(Gameplay<V::k70255c>, note_end) == 168);
static_assert(offsetof(Note<V::k70255c>, timeStart) == 24);
static_assert(offsetof(Note<V::k70255c>, timeEnd) == 28);
static_assert(offsetof(Note<V::k70255c>, pos_ptr) == 32);
static_assert(offsetof(Note<V::k70255c>, playSceneCtx) == 64);
static_assert(offsetof(Note<V::k70255c>, active) == 84);
static_assert(offsetof(Note<V::k70255c>, longTouchState) == 92);
static_assert(offsetof(NotePosition<V::k70255c>, xNorm) == 20);
static_assert(offsetof(ArcNote<V::k70255c>, isVoid) == 164);
static_assert(offsetof(ArcNote<V::k70255c>, activeNow) == 208);
static_assert(offsetof(NoteRuntimePos<V::k70255c>, runtimeX) == 212);
static_assert(offsetof(NoteRuntimePos<V::k70255c>, runtimeY) == 216);
static_assert(offsetof(HoldNote<V::k70255c>, headActivated) == 168);
static_assert(offsetof(TouchLike<V::k70255c>, sysId) == 12);
static_assert(offsetof(TouchLike<V::k70255c>, ndcX) == 28);
static_assert(offsetof(HttpRequest<V::k70255c>, type) == 12);
static_assert(offsetof(HttpRequest<V::k70255c>, bodyBegin) == 40);
static_assert(offsetof(HttpRequest<V::k70255c>, bodyEnd) == 48);
static_assert(offsetof(HttpResponse<V::k70255c>, request) == 16);
static_assert(offsetof(HttpResponse<V::k70255c>, succeed) == 24);
static_assert(offsetof(HttpResponse<V::k70255c>, bodyVec) == 32);
static_assert(offsetof(HttpResponse<V::k70255c>, statusCode) == 80);
static_assert(offsetof(Song<V::k70255c>, remote_pack) == 0x1C0);
static_assert(offsetof(Song<V::k70255c>, difficulty_pointers) == 0x228);
static_assert(offsetof(Song<V::k70255c>, difficulty_presence) == 0x250);
static_assert(offsetof(SongDifficulty<V::k70255c>, lock) == 0xF0);
static_assert(offsetof(SongDifficulty<V::k70255c>, rating_class) == 0x124);
static_assert(sizeof(SongDifficulty<V::k70255c>) == 0x128);
static_assert(offsetof(SongRegistryOwner<V::k70255c>, registry) == 32);

} // namespace verify

} // namespace arc_helper::cfg::layouts
