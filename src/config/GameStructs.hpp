#pragma once

#include <cstddef>
#include <cstdint>

#include "config/GameProfile.hpp"

namespace arc_autoplay::cfg::layouts {

// ============================================================================
//  Mirror structs of in-game C++ objects (on-heap, not constructed by us).
//
//  Each struct describes the memory layout at a known address.  Named fields
//  mark offsets whose purpose we have confirmed; unknown regions are explicit
//  uint8_t pad_XX[...] arrays so that sizeof() + offsetof() stay correct.
//
//  Version handling:
//    Every struct is templated on GameVersionId.  Currently both 6.12.11c and
//    6.13.2f share the same layout (verified by cross-version decomp diff in
//    Arc-RE/136-arcautoplay-version-upgrade-6.13.2f.md).  When a future
//    version changes a layout, override only the affected specialization.
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
} // namespace impl

template <GameVersionId Ver>
struct Note : impl::NoteBase {};

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
//  LogicArcNote (extends LogicNote)
// ---------------------------------------------------------------------------
namespace impl {
struct ArcNoteBase {
    uint8_t  pad_00[0x9C];
    int32_t  isVoid;                    // +0x9C  (156)
    uint8_t  pad_A0[0x28];
    uint8_t  activeNow;                 // +0xC8  (200)
};
} // namespace impl

template <GameVersionId Ver>
struct ArcNote : impl::ArcNoteBase {};

// ---------------------------------------------------------------------------
//  LogicHoldNote (extends LogicNote)
// ---------------------------------------------------------------------------
namespace impl {
struct HoldNoteBase {
    uint8_t pad_00[0xA0];
    uint8_t headActivated;              // +0xA0  (160)
};
} // namespace impl

template <GameVersionId Ver>
struct HoldNote : impl::HoldNoteBase {};

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

} // namespace verify

} // namespace arc_autoplay::cfg::layouts
