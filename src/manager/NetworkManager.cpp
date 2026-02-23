#include "manager/NetworkManager.hpp"

#include <algorithm>
#include <cstring>

#include "config/ModuleConfig.h"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_autoplay {
namespace {

thread_local network::ActiveRequestCtx t_req_ctx{};

size_t SafeStrnlen(const char *s, size_t max_len) {
    if (!s) return 0;
    size_t n = 0;
    while (n < max_len && s[n] != '\0') {
        n += 1;
    }
    return n;
}

bool ReadVectorBeginEnd(uintptr_t vec_obj, uintptr_t &out_begin, uintptr_t &out_end) {
    out_begin = 0;
    out_end = 0;
    if (!vec_obj) return false;
    if (!mem::ProcMaps::IsReadable(vec_obj, sizeof(uintptr_t) * 2)) return false;

    out_begin = mem::Read<uintptr_t>(vec_obj + 0);
    out_end = mem::Read<uintptr_t>(vec_obj + sizeof(uintptr_t));
    return out_end >= out_begin;
}

network::ActiveRequestCtx BuildActiveRequestCtx(uintptr_t http_client,
                                                uintptr_t http_response,
                                                char *curl_error_buf) {
    network::ActiveRequestCtx ctx{};
    ctx.http_client = http_client;
    ctx.http_response = http_response;
    ctx.http_request = http_response
                           ? mem::Read<uintptr_t>(http_response + cfg::network_block::kHttpResponse_request_ptr_off)
                           : 0;
    ctx.request_type = ctx.http_request
                           ? mem::Read<uint32_t>(ctx.http_request + cfg::network_block::kHttpRequest_type_u32_off)
                           : 0xFFFFFFFFu;
    ctx.request_body_ptr = ctx.http_request
                               ? mem::Read<uintptr_t>(ctx.http_request + cfg::network_block::kHttpRequest_body_begin_ptr_off)
                               : 0;
    const uintptr_t request_body_end = ctx.http_request
                                           ? mem::Read<uintptr_t>(ctx.http_request + cfg::network_block::kHttpRequest_body_end_ptr_off)
                                           : 0;
    ctx.request_body_len = (request_body_end >= ctx.request_body_ptr)
                               ? static_cast<size_t>(request_body_end - ctx.request_body_ptr)
                               : 0;
    ctx.response_body_vec = http_response ? (http_response + cfg::network_block::kHttpResponse_body_vec_off) : 0;
    ctx.curl_error_buf = curl_error_buf;
    ctx.url[0] = '\0';
    if (ctx.curl_error_buf) ctx.curl_error_buf[0] = '\0';
    return ctx;
}

} // namespace

NetworkManager &NetworkManager::Instance() {
    static NetworkManager manager;
    return manager;
}

bool NetworkManager::RegisterHandler(const char *name, int priority, HandlerFn fn) {
    if (!fn) return false;

    for (const auto &e : handlers_) {
        if (e.fn == fn) return true;
        if (name && e.name && std::strcmp(name, e.name) == 0) return true;
    }

    HandlerEntry entry{};
    entry.name = name;
    entry.priority = priority;
    entry.register_order = next_register_order_++;
    entry.fn = fn;
    handlers_.push_back(entry);

    std::stable_sort(handlers_.begin(), handlers_.end(), [](const HandlerEntry &a, const HandlerEntry &b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.register_order < b.register_order;
    });

    return EnsureHooksInstalled();
}

const char *NetworkManager::HttpMethodStr(uint32_t request_type) {
    return network::HttpMethodStr(request_type);
}

void NetworkManager::CopyAndSanitizeUrl(const char *url, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!url) return;

    const size_t n = SafeStrnlen(url, out_size - 1);
    std::memcpy(out, url, n);
    out[n] = '\0';

    if constexpr (cfg::network_block::kAuditStripQuery) {
        char *q = std::strchr(out, '?');
        if (q) *q = '\0';
    }
}

std::string NetworkManager::EscapeBytesForLog(const uint8_t *data, size_t len) {
    std::string out;
    if (!data || len == 0) return out;
    out.reserve(len * 2);

    auto hex_nibble = [](uint8_t v) -> char {
        return (v < 10) ? static_cast<char>('0' + v) : static_cast<char>('A' + (v - 10));
    };

    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        switch (b) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (b >= 0x20 && b <= 0x7E) {
                out.push_back(static_cast<char>(b));
            } else {
                out += "\\x";
                out.push_back(hex_nibble(static_cast<uint8_t>(b >> 4)));
                out.push_back(hex_nibble(static_cast<uint8_t>(b & 0x0F)));
            }
            break;
        }
    }

    return out;
}

NetworkManager::BufferView NetworkManager::ReadRequestBodyView(const HandlerArgs &args, size_t max_bytes) {
    BufferView view{};
    if (args.request_body_len == 0) {
        view.status = BufferViewStatus::Empty;
        return view;
    }
    if (!args.request_body_ptr) {
        view.status = BufferViewStatus::NullPtr;
        view.full_len = args.request_body_len;
        return view;
    }

    size_t show_len = args.request_body_len;
    if (max_bytes != 0 && show_len > max_bytes) show_len = max_bytes;

    view.full_len = args.request_body_len;
    view.show_len = show_len;
    if (show_len == 0) {
        view.status = BufferViewStatus::Empty;
        return view;
    }
    if (!mem::ProcMaps::IsReadable(args.request_body_ptr, show_len)) {
        view.status = BufferViewStatus::Unreadable;
        return view;
    }

    view.data = reinterpret_cast<const uint8_t *>(args.request_body_ptr);
    view.status = BufferViewStatus::Ok;
    return view;
}

NetworkManager::BufferView NetworkManager::ReadResponseBodyView(const HandlerArgs &args, size_t max_bytes) {
    BufferView view{};
    if (!args.response_body_vec) {
        view.status = BufferViewStatus::NullPtr;
        return view;
    }

    uintptr_t b0 = 0;
    uintptr_t b1 = 0;
    if (!ReadVectorBeginEnd(args.response_body_vec, b0, b1)) {
        view.status = BufferViewStatus::InvalidVector;
        return view;
    }

    const size_t full_len = static_cast<size_t>(b1 - b0);
    view.full_len = full_len;
    if (full_len == 0) {
        view.status = BufferViewStatus::Empty;
        return view;
    }

    size_t show_len = full_len;
    if (max_bytes != 0 && show_len > max_bytes) show_len = max_bytes;
    view.show_len = show_len;
    if (show_len == 0) {
        view.status = BufferViewStatus::Empty;
        return view;
    }
    if (!mem::ProcMaps::IsReadable(b0, show_len)) {
        view.status = BufferViewStatus::Unreadable;
        return view;
    }

    view.data = reinterpret_cast<const uint8_t *>(b0);
    view.status = BufferViewStatus::Ok;
    return view;
}

bool NetworkManager::EnsureHooksInstalled() {
    if (hooks_installed_) return true;
    if (handlers_.empty()) return true;

    hook_manager_.EnsureReady();
    lib_base_ = hook_manager_.GetLibBase();
    if (!lib_base_) return false;

    hook_manager_.InstallInlineHook(addr_httpclient_process_request_,
                                    cfg::network_block::kLibcocos2dcpp_HttpClient_processRequest,
                                    cfg::network_block::kSig_HttpClient_processRequest,
                                    HttpClientProcessRequestHook,
                                    "HttpClient_processRequest");

    hook_manager_.InstallInlineHook(addr_curl_easy_setopt_,
                                    cfg::network_block::kLibcocos2dcpp_Curl_easy_setopt,
                                    cfg::network_block::kSig_Curl_easy_setopt,
                                    CurlEasySetoptHook,
                                    "curl_easy_setopt",
                                    true);

    hooks_installed_ = hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(HttpClientProcessRequestHook)) &&
                       hook_manager_.HasOriginalForHook(reinterpret_cast<void *>(CurlEasySetoptHook));
    if (!hooks_installed_) {
        ARC_LOGE("NetworkManager: hook installation incomplete");
    }
    return hooks_installed_;
}

NetworkManager::DispatchResult NetworkManager::DispatchHandlers(HandlerArgs &args) {
    DispatchResult result{};

    for (const auto &entry : handlers_) {
        if (!entry.fn) continue;

        HandlerArgs candidate = args;
        const bool modified = entry.fn(candidate);
        if (!modified) continue;

        args = candidate;
        result.modified = true;
        if (args.blocked) {
            result.blocked = true;
            break;
        }
    }

    return result;
}

void NetworkManager::WriteCurlError(char *curl_error_buf, const char *msg) {
    if (!curl_error_buf) return;
    if (!msg) {
        curl_error_buf[0] = '\0';
        return;
    }

    constexpr size_t kMax = 255; // CURL_ERROR_SIZE - 1
    const size_t n = SafeStrnlen(msg, kMax);
    std::memcpy(curl_error_buf, msg, n);
    curl_error_buf[n] = '\0';
}

uint32_t NetworkManager::CurlEasySetoptHook(uintptr_t curl_handle, uint32_t option, uintptr_t param) {
    auto &manager = Instance();

    if (option == cfg::network_block::kCurlOpt_ErrorBuffer) {
        t_req_ctx.curl_error_buf = reinterpret_cast<char *>(param);
        if (t_req_ctx.curl_error_buf) t_req_ctx.curl_error_buf[0] = '\0';
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    if (option == cfg::network_block::kCurlOpt_WriteData) {
        t_req_ctx.response_body_vec = param;
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    if (option != cfg::network_block::kCurlOpt_URL) {
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    t_req_ctx.sequence = ++manager.next_sequence_;
    CopyAndSanitizeUrl(reinterpret_cast<const char *>(param), t_req_ctx.url, sizeof(t_req_ctx.url));

    HandlerArgs args = t_req_ctx.ToHandlerArgs(Phase::BeforeRequest);
    const DispatchResult dispatch = manager.DispatchHandlers(args);

    uintptr_t effective_param = param;
    if (dispatch.modified) {
        t_req_ctx.ApplyFromHandlerArgs(args);

        // URL argument can be rewritten by handlers. Always use the context URL
        // when a modification was accepted so next-stage behavior matches args.
        effective_param = reinterpret_cast<uintptr_t>(t_req_ctx.url);
    }

    if (dispatch.blocked) {
        const char *reason = args.block_reason[0] ? args.block_reason : "ArcAutoplay: blocked";
        WriteCurlError(args.curl_error_buf ? args.curl_error_buf : t_req_ctx.curl_error_buf, reason);
        return cfg::network_block::kCurlSetoptRetBlocked;
    }

    return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, effective_param);
}

int64_t NetworkManager::HttpClientProcessRequestHook(uintptr_t http_client, uintptr_t http_response, char *curl_error_buf) {
    const ActiveRequestCtx prev = t_req_ctx;
    t_req_ctx = BuildActiveRequestCtx(http_client, http_response, curl_error_buf);

    const int64_t ret = CALL_ORIG(HttpClientProcessRequestHook, http_client, http_response, curl_error_buf);

    if (http_response) {
        t_req_ctx.response_status_code = mem::Read<int64_t>(http_response + cfg::network_block::kHttpResponse_status_code_i64_off);
        t_req_ctx.response_ok = mem::Read<uint8_t>(http_response + cfg::network_block::kHttpResponse_succeed_u8_off);
    }

    HandlerArgs args = t_req_ctx.ToHandlerArgs(Phase::AfterRequest);
    const DispatchResult dispatch = Instance().DispatchHandlers(args);
    if (dispatch.modified) {
        t_req_ctx.ApplyFromHandlerArgs(args);

        // Apply editable response metadata back to HttpResponse.
        if (http_response) {
            mem::Write<int64_t>(http_response + cfg::network_block::kHttpResponse_status_code_i64_off,
                                args.response_status_code);
            mem::Write<uint8_t>(http_response + cfg::network_block::kHttpResponse_succeed_u8_off,
                                args.response_ok);
        }
    }

    t_req_ctx = prev;
    return ret;
}

} // namespace arc_autoplay
