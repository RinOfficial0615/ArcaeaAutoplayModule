#include "features/NetworkBlock.hpp"

#include <cstring>
#include <atomic>
#include <string_view>

#include "config/NetworkBlockConfig.h"
#include "manager/custom_chart/CustomChartGameplaySession.hpp"
#include "utils/Log.h"

namespace arc_helper {
namespace {

std::atomic_uint64_t g_blocked_count{0};
std::atomic_bool g_logged_block_active{false};

} // namespace

NetworkBlock &NetworkBlock::Instance() {
    static NetworkBlock feature;
    return feature;
}

NetworkBlock::NetworkBlock() : Feature("NetworkBlock") {}

void NetworkBlock::Install(const cfg::GameProfile &profile, bool isolation_armed) {
    (void)profile;
    isolation_armed_ = isolation_armed;
    if (installed_) return;
    if (!enabled_ && !isolation_armed_) {
        installed_ = true;
        return;
    }
    const bool ok = NetworkManager::Instance().RegisterHandler(
        Name(), cfg::network_block::kHandlerPriorityNetworkBlock, HandleNetworkRequest);

    installed_ = ok;
    ARC_LOGI("Handler registration %s", ok ? "OK" : "FAILED");
}

bool NetworkBlock::HandleNetworkRequest(NetworkManager::HandlerArgs &args) {
    if (args.phase != NetworkManager::Phase::BeforeRequest) return false;

    auto &feature = Instance();
    const auto decision = cfg::network_block::Evaluate(
        {
            .request_type = args.request_type,
            .url_truncated = args.url_truncated,
            .url_path = args.url_path,
        },
        {
            .ordinary_enabled = feature.enabled_,
            .isolation_active = feature.isolation_armed_ &&
                                CustomChartGameplaySession::Instance().IsActive(),
            .block_all_requests = feature.block_all_requests_,
            .block_all_non_get = feature.block_all_non_get_,
        });
    if (!decision.block) return false;
    const char *reason = decision.reason;

    args.blocked = true;
    if (reason) {
        // block_reason is a fixed char array, so the copy is clamped to leave
        // room for the terminator; string_view::copy reports how much it wrote.
        const size_t copied = std::string_view(reason).copy(args.block_reason,
                                                            sizeof(args.block_reason) - 1);
        args.block_reason[copied] = '\0';
    }

    const uint64_t blocked_count = g_blocked_count.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (g_logged_block_active.compare_exchange_strong(expected, true,
                                                       std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
        ARC_LOGI("Blocking enabled");
    }

    if (feature.blocked_log_limit_ == 0 || blocked_count <= feature.blocked_log_limit_) {
        ARC_LOGI("BLOCK #%llu %s %s (reason=%s)",
                 static_cast<unsigned long long>(blocked_count),
                 args.MethodStr(),
                 args.url[0] ? args.url : "(null)",
                 args.block_reason[0] ? args.block_reason : "blocked");
    }

    return true;
}

} // namespace arc_helper
