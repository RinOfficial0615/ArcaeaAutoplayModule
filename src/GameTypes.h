#pragma once

#include <cstddef>
#include <cstdint>

#include "AutoplayConfig.h"
#include "MemoryUtils.h"

namespace arc_autoplay::game {

class Object {
public:
    constexpr Object() = default;
    explicit constexpr Object(uintptr_t addr) : addr_(addr) {}

    constexpr uintptr_t addr() const { return addr_; }
    explicit constexpr operator bool() const { return addr_ != 0; }

protected:
    template <typename T>
    T Read(size_t off) const {
        return mem::Read<T>(addr_ + off);
    }

    template <typename T>
    void Write(size_t off, T value) const {
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
        if (Read<uint8_t>(cfg::kTimer_flag_u8_off)) {
            return Read<int32_t>(cfg::kTimer_msA_i32_off) - Read<int32_t>(cfg::kTimer_msB_i32_off);
        }

        const int v5 = Read<int32_t>(cfg::kTimer_msC_i32_off);
        const int v6 = Read<int32_t>(cfg::kTimer_msB_i32_off);
        const int v7 = (v5 <= 0) ? -3000 : 0;
        return v5 - v6 + v7;
    }
};

class Gameplay final : public Object {
public:
    using Object::Object;

    Timer timer() const { return Timer(ReadPtr(cfg::kGameplay_timer_off)); }

    uintptr_t *pendingNoteBegin() const {
        return reinterpret_cast<uintptr_t *>(ReadPtr(cfg::kGameplay_note_begin_off));
    }
    uintptr_t *pendingNoteEnd() const {
        return reinterpret_cast<uintptr_t *>(ReadPtr(cfg::kGameplay_note_end_off));
    }
};

class NotePosition final : public Object {
public:
    using Object::Object;

    float x_norm() const {
        return Read<float>(cfg::kNote_pos_xnorm_f32_off);
    }
};

class LogicNote : public Object {
public:
    using Object::Object;

    bool active() const { return *this && Read<uint8_t>(cfg::kNote_active_u8_off) != 0; }
    int timeStart() const { return Read<int32_t>(cfg::kNote_timeStart_i32_off); }
    int timeEnd() const { return Read<int32_t>(cfg::kNote_timeEnd_i32_off); }

    NotePosition pos() const { return NotePosition(ReadPtr(cfg::kNote_pos_ptr_off)); }

    uintptr_t vtable() const { return mem::Read<uintptr_t>(addr()); }

    uintptr_t vcall(size_t vtbl_off) const {
        const uintptr_t vt = vtable();
        if (!vt) return 0;
        return mem::Read<uintptr_t>(vt + vtbl_off);
    }

    // Itanium ABI: typeinfo ptr is stored at *(vptr - 8).
    uintptr_t typeinfo() const {
        const uintptr_t vptr = vtable();
        if (!vptr) return 0;
        return mem::Read<uintptr_t>(vptr - 8);
    }

    uintptr_t playSceneOrCtx() const { return ReadPtr(cfg::kNote_play_scene_ptr_off); }

    void clearLongTouchState() const { Write<uint16_t>(cfg::kLong_touch_state_u16_off, 0); }
};

class LogicHoldNote final : public LogicNote {
public:
    using LogicNote::LogicNote;

    void setHeadActivated(uint8_t v) const { Write<uint8_t>(cfg::kHold_headActivated_u8_off, v); }
};

class LogicArcNote final : public LogicNote {
public:
    using LogicNote::LogicNote;

    bool isVoid() const { return Read<int32_t>(cfg::kArc_isVoid_i32_off) != 0; }
    bool activeNow() const { return Read<uint8_t>(cfg::kArc_activeNow_u8_off) != 0; }

    float runtimeX() const { return Read<float>(cfg::kNote_runtime_x_f32_off); }
    float runtimeY() const { return Read<float>(cfg::kNote_runtime_y_f32_off); }
};

class TouchLike final : public Object {
public:
    using Object::Object;

    int sys_id() const { return Read<int32_t>(cfg::kTouch_sys_id_i32_off); }
};

} // namespace arc_autoplay::game
