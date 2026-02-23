#include "features/NetworkBlock.hpp"

#include <cstdint>
#include <cstring>

#include "config/ModuleConfig.h"
#include "config/NetworkBlockConfig.h"
#include "utils/Log.h"

namespace arc_autoplay {
namespace {

uint32_t g_blocked_count = 0;
bool g_logged_block_active = false;

uint8_t MethodBit(uint32_t req_type) {
    switch (req_type) {
    case 0:
        return cfg::network_block::kMethodGet;
    case 1:
        return cfg::network_block::kMethodPost;
    case 2:
        return cfg::network_block::kMethodPut;
    case 3:
        return cfg::network_block::kMethodDelete;
    default:
        return 0;
    }
}

bool EndsWith(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    const size_t sl = std::strlen(s);
    const size_t su = std::strlen(suffix);
    if (sl < su) return false;
    return std::memcmp(s + (sl - su), suffix, su) == 0;
}

bool EndsWithPath(const char *url, const char *path) {
    if (!url || !path) return false;
    if (EndsWith(url, path)) return true;

    const size_t n = std::strlen(path);
    if (n == 0 || n >= 255) return false;

    char tmp[256];
    std::memcpy(tmp, path, n);
    tmp[n] = '/';
    tmp[n + 1] = '\0';
    return EndsWith(url, tmp);
}

bool HasPathPrefix(const char *url, const char *path_prefix) {
    if (!url || !path_prefix) return false;
    const size_t n = std::strlen(path_prefix);
    if (n == 0) return false;

    const char *p = std::strstr(url, path_prefix);
    if (!p) return false;

    const char after = p[n];
    return after == '\0' || after == '/';
}

bool MatchRule(const cfg::network_block::NetworkBlockRule &rule, const char *url) {
    if (!url || !rule.pattern) return false;

    switch (rule.match_type) {
    case cfg::network_block::RuleMatchType::PathPrefix:
        return HasPathPrefix(url, rule.pattern);
    case cfg::network_block::RuleMatchType::PathSuffix:
        return EndsWithPath(url, rule.pattern);
    default:
        return false;
    }
}

bool ShouldBlock(const NetworkManager::HandlerArgs &args, const char **out_reason) {
    if (out_reason) *out_reason = "none";

    if (cfg::network_block::kBlockAllRequests) {
        if (out_reason) *out_reason = "all";
        return true;
    }
    if (cfg::network_block::kBlockAllNonGet && args.request_type != 0) {
        if (out_reason) *out_reason = "non-get";
        return true;
    }
    if (args.url[0] == '\0') {
        return false;
    }

    const uint8_t method = MethodBit(args.request_type);
    if (method == 0) return false;

    for (const auto &rule : cfg::network_block::kBlockRules) {
        if ((rule.method_mask & method) == 0) continue;
        if (!MatchRule(rule, args.url)) continue;
        if (out_reason) *out_reason = rule.reason ? rule.reason : "rule";
        return true;
    }

    return false;
}

} // namespace

NetworkBlock &NetworkBlock::Instance() {
    if constexpr (!cfg::module::kNetworkBlockEnabled) {
        static NetworkBlock disabled(false);
        return disabled;
    } else {
        static NetworkBlock enabled(true);
        return enabled;
    }
}

NetworkBlock::NetworkBlock(bool enabled) {
    if (!enabled) return;

    const bool ok = NetworkManager::Instance().RegisterHandler(
        "NetworkBlock", cfg::network_block::kHandlerPriorityNetworkBlock, HandleNetworkRequest);

    ARC_LOGI("NetworkBlock: handler registration %s", ok ? "OK" : "FAILED");
}

bool NetworkBlock::HandleNetworkRequest(NetworkManager::HandlerArgs &args) {
    if (args.phase != NetworkManager::Phase::BeforeRequest) return false;

    const char *reason = "none";
    if (!ShouldBlock(args, &reason)) return false;

    args.blocked = true;
    if (reason) {
        const size_t n = std::strlen(reason);
        const size_t max_n = sizeof(args.block_reason) - 1;
        const size_t copy_n = (n < max_n) ? n : max_n;
        std::memcpy(args.block_reason, reason, copy_n);
        args.block_reason[copy_n] = '\0';
    }

    g_blocked_count += 1;
    if (!g_logged_block_active) {
        g_logged_block_active = true;
        ARC_LOGI("NetworkBlock: blocking enabled");
    }

    if (cfg::network_block::kBlockedLogLimit == 0 || g_blocked_count <= cfg::network_block::kBlockedLogLimit) {
        ARC_LOGI("NetworkBlock: BLOCK #%u %s %s (reason=%s)",
                 g_blocked_count,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(null)",
                 args.block_reason[0] ? args.block_reason : "blocked");
    }

    return true;
}

} // namespace arc_autoplay
