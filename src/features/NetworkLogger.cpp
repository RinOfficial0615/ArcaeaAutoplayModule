#include "features/NetworkLogger.hpp"

#include "config/ModuleConfig.h"
#include "config/NetworkBlockConfig.h"
#include "utils/Log.h"

namespace arc_autoplay {

namespace {

void MaybeLogRequestBody(const NetworkManager::HandlerArgs &args) {
    if constexpr (!cfg::network_block::kAuditLogBody) return;
    if (args.request_body_len == 0) return;

    if constexpr (cfg::network_block::kAuditLogBodyOnlyNonGet) {
        if (args.request_type == 0) return;
    }

    const auto body = NetworkManager::ReadRequestBodyView(args, cfg::network_block::kAuditBodyMaxBytes);
    switch (body.status) {
    case NetworkManager::BufferViewStatus::Ok: {
        const auto escaped = NetworkManager::EscapeBytesForLog(body.data, body.show_len);
        const bool truncated = body.show_len < body.full_len;
        ARC_LOGI("NetworkLogger: BODY #%u %s %s (len=%zu show=%zu)%s body=%s",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(null)",
                 body.full_len,
                 body.show_len,
                 truncated ? " [TRUNC]" : "",
                 escaped.c_str());
        break;
    }
    case NetworkManager::BufferViewStatus::NullPtr:
        ARC_LOGI("NetworkLogger: BODY #%u %s %s (ptr=null, len=%zu)",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(null)",
                 args.request_body_len);
        break;
    case NetworkManager::BufferViewStatus::Unreadable:
        ARC_LOGI("NetworkLogger: BODY #%u %s %s (unreadable ptr=%p, len=%zu)",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(null)",
                 reinterpret_cast<void *>(args.request_body_ptr),
                 args.request_body_len);
        break;
    default:
        break;
    }
}

void MaybeLogResponse(const NetworkManager::HandlerArgs &args) {
    if constexpr (!cfg::network_block::kAuditLogResponse) return;

    if (args.curl_error_buf && args.curl_error_buf[0] != '\0') {
        ARC_LOGI("NetworkLogger: ERR  #%u %s %s err=%s",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(unknown)",
                 args.curl_error_buf);
    }

    const auto resp = NetworkManager::ReadResponseBodyView(args, cfg::network_block::kAuditResponseMaxBytes);
    switch (resp.status) {
    case NetworkManager::BufferViewStatus::Ok: {
        const auto escaped = NetworkManager::EscapeBytesForLog(resp.data, resp.show_len);
        const bool truncated = resp.show_len < resp.full_len;
        ARC_LOGI("NetworkLogger: RESP #%u %s %s (code=%lld ok=%u len=%zu show=%zu)%s resp=%s",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(unknown)",
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok),
                 resp.full_len,
                 resp.show_len,
                 truncated ? " [TRUNC]" : "",
                 escaped.c_str());
        break;
    }
    case NetworkManager::BufferViewStatus::NullPtr:
        ARC_LOGI("NetworkLogger: RESP #%u %s %s (code=%lld ok=%u) (no body vec)",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(unknown)",
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok));
        break;
    case NetworkManager::BufferViewStatus::InvalidVector:
        ARC_LOGI("NetworkLogger: RESP #%u %s %s (code=%lld ok=%u) (bad vec=%p)",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(unknown)",
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok),
                 reinterpret_cast<void *>(args.response_body_vec));
        break;
    case NetworkManager::BufferViewStatus::Unreadable:
        ARC_LOGI("NetworkLogger: RESP #%u %s %s (code=%lld ok=%u) (unreadable body)",
                 args.sequence,
                 args.MethodStr(),
                 args.url[0] ? args.url : "(unknown)",
                 static_cast<long long>(args.response_status_code),
                 static_cast<unsigned int>(args.response_ok));
        break;
    default:
        break;
    }
}

} // namespace

NetworkLogger &NetworkLogger::Instance() {
    if constexpr (!cfg::module::kNetworkLoggerEnabled) {
        static NetworkLogger disabled(false);
        return disabled;
    } else {
        static NetworkLogger enabled(true);
        return enabled;
    }
}

NetworkLogger::NetworkLogger(bool enabled) {
    if (!enabled) return;

    const bool ok = NetworkManager::Instance().RegisterHandler(
        "NetworkLogger", cfg::network_block::kHandlerPriorityNetworkLogger, HandleNetworkRequest);

    ARC_LOGI("NetworkLogger: handler registration %s", ok ? "OK" : "FAILED");
}

bool NetworkLogger::HandleNetworkRequest(NetworkManager::HandlerArgs &args) {
    if (args.sequence == 0) return false;

    if (cfg::network_block::kAuditLogLimit != 0 && args.sequence > cfg::network_block::kAuditLogLimit) {
        return false;
    }

    if (args.phase == NetworkManager::Phase::BeforeRequest) {
        if constexpr (cfg::network_block::kAuditLogAllRequests) {
            ARC_LOGI("NetworkLogger: URL #%u %s %s (body=%zu)",
                     args.sequence,
                     args.MethodStr(),
                     args.url[0] ? args.url : "(null)",
                     args.request_body_len);
        }
        MaybeLogRequestBody(args);
        return false;
    }

    if (args.phase == NetworkManager::Phase::AfterRequest) {
        MaybeLogResponse(args);
        return false;
    }

    return false;
}

} // namespace arc_autoplay
