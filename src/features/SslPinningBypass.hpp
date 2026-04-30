#pragma once

#include <cstdint>

#include "features/Feature.hpp"
#include "config/GameProfile.hpp"

namespace arc_autoplay {

class SslPinningBypass : public Feature {
public:
    static SslPinningBypass &Instance();

private:
    SslPinningBypass();

    void EnsurePatched();

    bool patched_ = false;
};

} // namespace arc_autoplay
