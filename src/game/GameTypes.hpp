#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "config/AutoplayConfig.h"
#include "utils/MemoryUtils.hpp"

namespace arc_helper::game {

class Object {
public:
    constexpr Object() = default;
    explicit constexpr Object(uintptr_t addr) : addr_(addr) {}

    constexpr uintptr_t Addr() const { return addr_; }
    explicit constexpr operator bool() const { return addr_ != 0; }

protected:
    template <typename T>
    T Read(size_t off) const {
        if (addr_ == 0 || off > std::numeric_limits<uintptr_t>::max() - addr_) return T{};
        return mem::Read<T>(addr_ + off);
    }

    template <typename T>
    void Write(size_t off, T value) const {
        if (addr_ == 0 || off > std::numeric_limits<uintptr_t>::max() - addr_) return;
        mem::Write<T>(addr_ + off, value);
    }

    uintptr_t ReadPtr(size_t off) const { return Read<uintptr_t>(off); }

private:
    uintptr_t addr_ = 0;
};

class Timer final : public Object {
public:
    using Object::Object;

    int NowMs() const {
        if (!*this) return 0;
        if (Read<uint8_t>(cfg::autoplay::kTimer_flag_u8_off)) {
            return Read<int32_t>(cfg::autoplay::kTimer_msA_i32_off) - Read<int32_t>(cfg::autoplay::kTimer_msB_i32_off);
        }

        const int v5 = Read<int32_t>(cfg::autoplay::kTimer_msC_i32_off);
        const int v6 = Read<int32_t>(cfg::autoplay::kTimer_msB_i32_off);
        const int v7 = (v5 <= 0) ? -3000 : 0;
        return v5 - v6 + v7;
    }
};

class Gameplay final : public Object {
public:
    using Object::Object;

    Timer GetTimer() const { return Timer(ReadPtr(cfg::autoplay::kGameplay_timer_off)); }

    uintptr_t *PendingNoteBegin() const {
        return reinterpret_cast<uintptr_t *>(ReadPtr(cfg::autoplay::kGameplay_note_begin_off));
    }
    uintptr_t *PendingNoteEnd() const {
        return reinterpret_cast<uintptr_t *>(ReadPtr(cfg::autoplay::kGameplay_note_end_off));
    }
};

class NotePosition final : public Object {
public:
    using Object::Object;

    float XNorm() const {
        return Read<float>(cfg::autoplay::kNote_pos_xnorm_f32_off);
    }
};

class LogicNote : public Object {
public:
    using Object::Object;

    bool Active() const { return *this && Read<uint8_t>(cfg::autoplay::kNote_active_u8_off) != 0; }
    int TimeStart() const { return Read<int32_t>(cfg::autoplay::kNote_timeStart_i32_off); }
    int TimeEnd() const { return Read<int32_t>(cfg::autoplay::kNote_timeEnd_i32_off); }

    NotePosition Pos() const { return NotePosition(ReadPtr(cfg::autoplay::kNote_pos_ptr_off)); }

    uintptr_t Vtable() const {
        if (!*this) return 0;
        return mem::Read<uintptr_t>(Addr());
    }

    uintptr_t Vcall(size_t vtbl_off) const {
        const uintptr_t vt = Vtable();
        if (!vt || vtbl_off > std::numeric_limits<uintptr_t>::max() - vt) return 0;
        return mem::Read<uintptr_t>(vt + vtbl_off);
    }

    // Itanium ABI: typeinfo ptr is stored at *(vptr - 8).
    uintptr_t Typeinfo() const {
        const uintptr_t vptr = Vtable();
        if (!vptr || vptr < sizeof(uintptr_t)) return 0;
        return mem::Read<uintptr_t>(vptr - 8);
    }

    uintptr_t PlaySceneOrCtx() const { return ReadPtr(cfg::autoplay::kNote_play_scene_ptr_off); }

    void clearLongTouchState() const { Write<uint16_t>(cfg::autoplay::kLong_touch_state_u16_off, 0); }
};

class LogicHoldNote final : public LogicNote {
public:
    using LogicNote::LogicNote;

    void SetHeadActivated(uint8_t v) const {
        Write<uint8_t>(cfg::autoplay::HoldHeadActivatedOffset(cfg::GetRuntimeLayoutVersion()), v);
    }
};

class LogicArcNote final : public LogicNote {
public:
    using LogicNote::LogicNote;

    bool IsVoid() const {
        return Read<int32_t>(cfg::autoplay::ArcIsVoidOffset(cfg::GetRuntimeLayoutVersion())) != 0;
    }
    bool ActiveNow() const {
        return Read<uint8_t>(cfg::autoplay::ArcActiveNowOffset(cfg::GetRuntimeLayoutVersion())) != 0;
    }

    float RuntimeX() const {
        return Read<float>(cfg::autoplay::NoteRuntimeXOffset(cfg::GetRuntimeLayoutVersion()));
    }
    float RuntimeY() const {
        return Read<float>(cfg::autoplay::NoteRuntimeYOffset(cfg::GetRuntimeLayoutVersion()));
    }
};

class TouchLike final : public Object {
public:
    using Object::Object;

    int SysId() const { return Read<int32_t>(cfg::autoplay::kTouch_sys_id_i32_off); }
};

} // namespace arc_helper::game
