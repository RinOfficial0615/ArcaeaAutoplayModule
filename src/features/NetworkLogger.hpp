#pragma once

#include "features/Feature.hpp"
#include "manager/NetworkManager.hpp"

namespace arc_autoplay {

// Registers a high-priority audit handler in NetworkManager.
//
// This handler logs request/response information and never blocks traffic.
class NetworkLogger : public Feature {
public:
    static NetworkLogger &Instance();

private:
    explicit NetworkLogger(bool enabled);

    static bool HandleNetworkRequest(NetworkManager::HandlerArgs &args);
};

} // namespace arc_autoplay
