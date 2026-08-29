#include "features/NetworkLogger.hpp"

#include <cstring>
#include <string>
#include <string_view>

#include "config/NetworkBlockConfig.h"
#include "utils/Log.h"

namespace arc_helper {
namespace {

// The logging macros want a NUL-terminated buffer, hence the final copy; the
// truncation itself stays on a view. substr(0, find('?')) already means "the
// whole string" when there is no query, so the npos case needs no branch.
std::string UrlForLog(const char *url, bool strip_query) {
    std::string_view view = url && url[0] ? std::string_view(url) : "(unknown)";
    if (strip_query) view = view.substr(0, view.find('?'));
    return std::string(view);
}

// Shared parenthetical note for the non-Ok response body statuses.
const char *ResponseBodyStatusNote(NetworkManager::BufferViewStatus status) {
    switch (status) {
    case NetworkManager::BufferViewStatus::NullPtr: return "no body vec";
    case NetworkManager::BufferViewStatus::InvalidVector: return "bad body vector";
    case NetworkManager::BufferViewStatus::Unreadable: return "unreadable body";
    default: return nullptr;
    }
}

} // namespace

NetworkLogger &NetworkLogger::Instance() {
    static NetworkLogger feature;
    return feature;
}

NetworkLogger::NetworkLogger() : Feature("NetworkLogger") {}

void NetworkLogger::Install(const cfg::GameProfile &profile) {
    (void)profile;
    if (installed_) return;
    if (!enabled_) {
        installed_ = true;
        return;
    }

    const bool ok = NetworkManager::Instance().RegisterHandler(
        Name(), cfg::network_block::kHandlerPriorityNetworkLogger,
        HandleNetworkRequest);
    installed_ = ok;
    ARC_LOGI("Handler registration %s", ok ? "OK" : "FAILED");
}

bool NetworkLogger::HandleNetworkRequest(NetworkManager::HandlerArgs &args) {
    auto &feature = Instance();
    if (!feature.enabled_ || args.sequence == 0) return false;
    if (feature.request_limit_ != 0 && args.sequence > feature.request_limit_) return false;

    const std::string url = UrlForLog(args.url, feature.strip_query_);
    if (args.phase == NetworkManager::Phase::BeforeRequest) {
        if (feature.log_all_requests_) {
            ARC_LOGI("URL #%u %s %s (body=%zu)",
                     args.sequence, args.MethodStr(), url.c_str(), args.request_body_len);
        }
        if (!feature.log_request_body_ || args.request_body_len == 0 ||
            (feature.request_body_only_non_get_ && args.request_type == 0)) {
            return false;
        }

        const auto body = args.request_body_view.Limit(feature.body_capture_limit_);
        switch (body.status) {
        case NetworkManager::BufferViewStatus::Ok: {
            const auto escaped = NetworkManager::EscapeBytesForLog(body.data, body.show_len);
            ARC_LOGI("BODY #%u %s %s (len=%zu shown=%zu trunc=%u) body=%s",
                     args.sequence, args.MethodStr(), url.c_str(), body.full_len,
                     body.show_len, static_cast<unsigned int>(body.Truncated()),
                     escaped.c_str());
            break;
        }
        case NetworkManager::BufferViewStatus::NullPtr:
            ARC_LOGI("BODY #%u %s %s (ptr=null, len=%zu)",
                     args.sequence, args.MethodStr(), url.c_str(), args.request_body_len);
            break;
        case NetworkManager::BufferViewStatus::Unreadable:
            ARC_LOGI("BODY #%u %s %s (unreadable, len=%zu)",
                     args.sequence, args.MethodStr(), url.c_str(), args.request_body_len);
            break;
        default:
            break;
        }
        return false;
    }

    if (args.phase != NetworkManager::Phase::AfterRequest || !feature.log_response_) {
        return false;
    }
    if (args.CurlError()[0] != '\0') {
        ARC_LOGI("ERR  #%u %s %s err=%s",
                 args.sequence, args.MethodStr(), url.c_str(), args.CurlError());
    }

    const auto response = args.response_body_view.Limit(feature.body_capture_limit_);
    switch (response.status) {
    case NetworkManager::BufferViewStatus::Ok: {
        const auto escaped = NetworkManager::EscapeBytesForLog(response.data, response.show_len);
        ARC_LOGI("RESP #%u %s %s (code=%lld ok=%u len=%zu shown=%zu trunc=%u) resp=%s",
                 args.sequence, args.MethodStr(), url.c_str(),
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok), response.full_len,
                 response.show_len, static_cast<unsigned int>(response.Truncated()),
                 escaped.c_str());
        break;
    }
    default: {
        const char *note = ResponseBodyStatusNote(response.status);
        if (!note) break;
        ARC_LOGI("RESP #%u %s %s (code=%lld ok=%u) (%s)",
                 args.sequence, args.MethodStr(), url.c_str(),
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok), note);
        break;
    }
    }
    return false;
}

} // namespace arc_helper
