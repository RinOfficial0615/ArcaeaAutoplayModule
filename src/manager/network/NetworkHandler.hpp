#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config/NetworkBlockConfig.h"

namespace arc_autoplay::network {

enum class Phase : uint8_t {
    BeforeRequest = 0,
    AfterRequest = 1,
};

inline const char *HttpMethodStr(uint32_t request_type) {
    switch (request_type) {
    case 0:
        return "GET";
    case 1:
        return "POST";
    case 2:
        return "PUT";
    case 3:
        return "DELETE";
    default:
        return "UNK";
    }
}

struct HandlerArgs;

// Mutable per-request context tracked by NetworkManager hook callbacks.
struct ActiveRequestCtx {
    uintptr_t http_client = 0;
    uintptr_t http_request = 0;
    uintptr_t http_response = 0;

    uint32_t request_type = 0xFFFFFFFFu; // 0=GET,1=POST,2=PUT,3=DELETE
    uintptr_t request_body_ptr = 0;
    size_t request_body_len = 0;

    uintptr_t response_body_vec = 0; // std::vector<char>
    int64_t response_status_code = -1;
    uint8_t response_ok = 0;

    char *curl_error_buf = nullptr;
    uint32_t sequence = 0;

    char url[cfg::network_block::kAuditUrlMaxLen + 1] = {0};

    HandlerArgs ToHandlerArgs(Phase phase) const;
    void ApplyFromHandlerArgs(const HandlerArgs &args);
};

// Mutable args passed into handlers.
//
// Handlers may edit fields and return `true` to accept modifications.
// Returning `false` discards all edits.
struct HandlerArgs {
    Phase phase = Phase::BeforeRequest;

    uintptr_t http_client = 0;
    uintptr_t http_request = 0;
    uintptr_t http_response = 0;

    uint32_t request_type = 0xFFFFFFFFu;
    uintptr_t request_body_ptr = 0;
    size_t request_body_len = 0;

    uintptr_t response_body_vec = 0;
    int64_t response_status_code = -1;
    uint8_t response_ok = 0;

    char *curl_error_buf = nullptr;
    uint32_t sequence = 0;

    char url[cfg::network_block::kAuditUrlMaxLen + 1] = {0};

    bool blocked = false;
    char block_reason[96] = {0};

    HandlerArgs() = default;
    HandlerArgs(const ActiveRequestCtx &ctx, Phase p) : phase(p) {
        http_client = ctx.http_client;
        http_request = ctx.http_request;
        http_response = ctx.http_response;
        request_type = ctx.request_type;
        request_body_ptr = ctx.request_body_ptr;
        request_body_len = ctx.request_body_len;
        response_body_vec = ctx.response_body_vec;
        response_status_code = ctx.response_status_code;
        response_ok = ctx.response_ok;
        curl_error_buf = ctx.curl_error_buf;
        sequence = ctx.sequence;
        std::memcpy(url, ctx.url, sizeof(url));
    }

    void ApplyToContext(ActiveRequestCtx &ctx) const {
        ctx.http_client = http_client;
        ctx.http_request = http_request;
        ctx.http_response = http_response;
        ctx.request_type = request_type;
        ctx.request_body_ptr = request_body_ptr;
        ctx.request_body_len = request_body_len;
        ctx.response_body_vec = response_body_vec;
        ctx.response_status_code = response_status_code;
        ctx.response_ok = response_ok;
        ctx.curl_error_buf = curl_error_buf;
        ctx.sequence = sequence;
        std::memcpy(ctx.url, url, sizeof(ctx.url));
    }

    const char *MethodStr() const { return HttpMethodStr(request_type); }
};

inline HandlerArgs ActiveRequestCtx::ToHandlerArgs(Phase phase) const {
    return HandlerArgs(*this, phase);
}

inline void ActiveRequestCtx::ApplyFromHandlerArgs(const HandlerArgs &args) {
    args.ApplyToContext(*this);
}

using HandlerFn = bool (*)(HandlerArgs &args);

enum class BufferViewStatus : uint8_t {
    Ok,
    Empty,
    NullPtr,
    Unreadable,
    InvalidVector,
};

struct BufferView {
    const uint8_t *data = nullptr;
    size_t full_len = 0;
    size_t show_len = 0;
    BufferViewStatus status = BufferViewStatus::Empty;
};

} // namespace arc_autoplay::network
