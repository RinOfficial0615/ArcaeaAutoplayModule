#pragma once

#include "features/Feature.hpp"
#include "manager/NetworkManager.hpp"

namespace arc_autoplay {

// Registers the lowest-priority block policy handler in NetworkManager.
//
// This feature does not install hooks directly; it only decides whether a
// request should be blocked based on configured URL/method rules.
class NetworkBlock : public Feature {
public:
    static NetworkBlock &Instance();

private:
    explicit NetworkBlock(bool enabled);

    static bool HandleNetworkRequest(NetworkManager::HandlerArgs &args);
};

} // namespace arc_autoplay
