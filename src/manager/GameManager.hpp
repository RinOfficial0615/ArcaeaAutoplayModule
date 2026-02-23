#pragma once

#include <atomic>
#include <cstdint>

namespace arc_autoplay {

class GameManager {
public:
    static GameManager &Instance();

    uintptr_t GetGameLibBase() const;
    uintptr_t GetOrFindGameLibBase();

private:
    GameManager() = default;

    std::atomic<uintptr_t> game_lib_base_{0};
};

} // namespace arc_autoplay
