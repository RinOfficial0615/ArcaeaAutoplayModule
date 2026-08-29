#include "manager/NetworkManager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

#include "config/ModuleConfig.h"
#include "manager/GameVersionManager.hpp"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_helper {
namespace {

thread_local network::ActiveRequestCtx t_req_ctx{};

size_t SafeStrnlen(const char *s, size_t max_len) {
    if (!s) return 0;
    if (max_len != 0 &&
        mem::ProcMaps::IsReadable(reinterpret_cast<uintptr_t>(s), max_len)) {
        const std::string_view bounded(s, max_len);
        const size_t terminator = bounded.find('\0');
        return terminator == std::string_view::npos ? max_len : terminator;
    }
    size_t n = 0;
    while (n < max_len && mem::ProcMaps::IsReadable(reinterpret_cast<uintptr_t>(s + n), 1)) {
        if (s[n] == '\0') break;
        n += 1;
    }
    return n;
}

size_t EffectiveBodyLimit(size_t requested) {
    constexpr size_t kHardLimit = cfg::network_block::kNetworkBodyCaptureMax;
    return requested == 0 ? kHardLimit : std::min(requested, kHardLimit);
}

uint32_t NextSequence(std::atomic_uint32_t &counter) {
    uint32_t current = counter.load(std::memory_order_relaxed);
    for (;;) {
        uint32_t next = current + 1;
        if (next == 0) next = 1;
        if (counter.compare_exchange_weak(current, next,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
            return next;
        }
    }
}

template <typename T>
bool ReadObject(uintptr_t object, size_t offset, T &value) {
    if (object == 0 || offset > std::numeric_limits<uintptr_t>::max() - object) return false;
    const auto result = mem::RuntimeMemory::Process().Read<T>(object + offset);
    if (!result) return false;
    value = *result;
    return true;
}

template <typename T>
bool WriteObject(uintptr_t object, size_t offset, const T &value) {
    if (object == 0 || offset > std::numeric_limits<uintptr_t>::max() - object) return false;
    return mem::RuntimeMemory::Process().Write<T>(object + offset, value).has_value();
}

bool ReadVectorBeginEnd(uintptr_t vec_obj, uintptr_t &out_begin, uintptr_t &out_end) {
    out_begin = 0;
    out_end = 0;
    if (!vec_obj) return false;
    if (!ReadObject(vec_obj, 0, out_begin) ||
        !ReadObject(vec_obj, sizeof(uintptr_t), out_end) || out_end < out_begin) {
        return false;
    }
    const uintptr_t span = out_end - out_begin;
    if (span == 0) return true;
    const uintptr_t checked = std::min<uintptr_t>(
        span, cfg::network_block::kNetworkBodyCaptureMax);
    return mem::ProcMaps::IsReadable(out_begin, static_cast<size_t>(checked));
}

network::ActiveRequestCtx BuildActiveRequestCtx(uintptr_t http_client,
                                                 uintptr_t http_response,
                                                 char *curl_error_buf) {
    network::ActiveRequestCtx ctx{};
    ctx.active = true;
    ctx.http_client = http_client;
    ctx.http_response = http_response;
    (void)ReadObject(http_response,
                     cfg::network_block::kHttpResponse_request_ptr_off,
                     ctx.http_request);
    ctx.request_type = 0xFFFFFFFFu;
    if (ctx.http_request) {
        (void)ReadObject(ctx.http_request,
                         cfg::network_block::kHttpRequest_type_u32_off,
                         ctx.request_type);
    }
    (void)ReadObject(ctx.http_request,
                     cfg::network_block::kHttpRequest_body_begin_ptr_off,
                     ctx.request_body_ptr);
    uintptr_t request_body_end = 0;
    (void)ReadObject(ctx.http_request,
                     cfg::network_block::kHttpRequest_body_end_ptr_off,
                     request_body_end);
    const uintptr_t request_body_delta = request_body_end >= ctx.request_body_ptr
                                             ? request_body_end - ctx.request_body_ptr
                                             : 0;
    ctx.request_body_len = ctx.request_body_ptr &&
                                   request_body_delta <= std::numeric_limits<size_t>::max()
                               ? static_cast<size_t>(request_body_delta)
                               : 0;
    ctx.response_body_vec = http_response &&
                                    cfg::network_block::kHttpResponse_body_vec_off <=
                                        std::numeric_limits<uintptr_t>::max() - http_response
                                ? http_response + cfg::network_block::kHttpResponse_body_vec_off
                                : 0;
    ctx.curl_error_buf = curl_error_buf;
    ctx.url[0] = '\0';
    ctx.url_path[0] = '\0';
    if (ctx.curl_error_buf &&
        mem::ProcMaps::IsWritable(reinterpret_cast<uintptr_t>(ctx.curl_error_buf),
                                  cfg::network_block::kCurlErrorMaxLen + 1)) {
        ctx.curl_error_buf[0] = '\0';
    }
    return ctx;
}

bool BindTrackedCurlHandle(network::ActiveRequestCtx &ctx, uintptr_t curl_handle) {
    if (!ctx.active || curl_handle == 0) return false;
    if (ctx.curl_handle == 0) ctx.curl_handle = curl_handle;
    return ctx.curl_handle == curl_handle;
}

} // namespace

NetworkManager &NetworkManager::Instance() {
    static NetworkManager manager;
    return manager;
}

bool NetworkManager::RegisterHandler(std::string_view name, int priority, HandlerFn fn) {
    if (!fn) return false;

    {
        std::scoped_lock lock(handler_registry_mutex_);
        auto current = std::atomic_load_explicit(&handler_snapshot_, std::memory_order_acquire);

        const std::string_view handler_name = name;
        if (!current->Contains(handler_name, fn)) {
            const network::HandlerSnapshot::Entry entry{
                std::string(handler_name), priority, next_register_order_++, fn};
            const auto next = current->With(entry);
            std::atomic_store_explicit(&handler_snapshot_, next, std::memory_order_release);
        }
    }

    return EnsureHooksInstalled();
}

const char *NetworkManager::HttpMethodStr(uint32_t request_type) {
    return network::HttpMethodStr(request_type);
}

bool NetworkManager::CopyAndSanitizeUrl(const char *url, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!url) return false;

    const size_t observed = SafeStrnlen(url, out_size);
    const bool truncated = observed == out_size;
    const size_t copied = std::min(observed, out_size - 1);
    std::memcpy(out, url, copied);
    out[copied] = '\0';
    return truncated;
}

static void CopyUrlPath(const char *url, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!url) return;

    const size_t length = SafeStrnlen(url, cfg::network_block::kUrlMaxLen);
    const std::string_view value(url, length);
    size_t path_start = 0;
    const size_t scheme = value.find("://");
    if (scheme != std::string_view::npos) {
        path_start = value.find('/', scheme + 3);
        if (path_start == std::string_view::npos) path_start = value.size();
    }
    if (path_start == value.size()) {
        if (out_size > 1) {
            out[0] = '/';
            out[1] = '\0';
        }
        return;
    }

    // "No query and no fragment" comes back as npos, which substr already
    // reads as "to the end"; copy() then clamps the path into the fixed output
    // buffer and reports how many bytes it wrote so we can terminate.
    const std::string_view path = value.substr(path_start);
    const size_t copied = path.substr(0, path.find_first_of("?#")).copy(out, out_size - 1);
    out[copied] = '\0';
}

static void NormalizeHandlerStrings(network::HandlerArgs &args) {
    const bool missing_url_terminator =
        SafeStrnlen(args.url, sizeof(args.url)) == sizeof(args.url);
    args.url[sizeof(args.url) - 1] = '\0';
    args.url_path[sizeof(args.url_path) - 1] = '\0';
    args.block_reason[sizeof(args.block_reason) - 1] = '\0';
    args.url_truncated = args.url_truncated || missing_url_terminator;
}

std::string NetworkManager::EscapeBytesForLog(const uint8_t *data, size_t len) {
    std::string out;
    if (!data || len == 0) return out;
    const size_t bounded_len = std::min(len, cfg::network_block::kNetworkBodyCaptureMax);
    out.reserve(bounded_len * 2);

    auto hex_nibble = [](uint8_t v) -> char {
        return (v < 10) ? static_cast<char>('0' + v) : static_cast<char>('A' + (v - 10));
    };

    for (const uint8_t b : std::span(data, bounded_len)) {
        switch (b) {
        case '\\':
            out.push_back('\\');
            out.push_back('\\');
            break;
        case '"':
            out.push_back('\\');
            out.push_back('"');
            break;
        case '\n':
            out.push_back('\\');
            out.push_back('n');
            break;
        case '\r':
            out.push_back('\\');
            out.push_back('r');
            break;
        case '\t':
            out.push_back('\\');
            out.push_back('t');
            break;
        default:
            if (b >= 0x20 && b <= 0x7E) {
                out.push_back(static_cast<char>(b));
            } else {
                out.push_back('\\');
                out.push_back('x');
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

    const size_t limit = EffectiveBodyLimit(max_bytes);
    size_t show_len = std::min(args.request_body_len, limit);

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

    const uintptr_t span = b1 - b0;
    if (span > std::numeric_limits<size_t>::max()) {
        view.status = BufferViewStatus::InvalidLength;
        return view;
    }
    const size_t full_len = static_cast<size_t>(span);
    view.full_len = full_len;
    if (full_len == 0) {
        view.status = BufferViewStatus::Empty;
        return view;
    }

    const size_t limit = EffectiveBodyLimit(max_bytes);
    size_t show_len = std::min(full_len, limit);
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
    std::scoped_lock install_lock(hook_install_mutex_);
    if (hooks_installed_.load(std::memory_order_acquire)) return true;
    const auto snapshot = std::atomic_load_explicit(&handler_snapshot_, std::memory_order_acquire);
    if (!snapshot || snapshot->Entries().empty()) return true;

    auto &version_manager = GameVersionManager::Instance();
    version_manager.EnsureInstalled();
    const auto *profile = version_manager.GetActiveProfile();
    if (!profile) return false;
    if (!profile->capabilities.network || !profile->network.httpclient_process_request ||
        !profile->network.curl_easy_setopt) {
        ARC_LOGE("Capability unavailable for %s", profile->version_name);
        return false;
    }

    hook_manager_.EnsureReady();
    const uintptr_t lib_base = hook_manager_.GetLibBase();
    if (!lib_base) return false;

    std::array<HookManager::InlineHookRegistration, 2> registrations = {
        hook_manager_.RegisterInlineHook(addr_httpclient_process_request_,
                                         profile->network.httpclient_process_request,
                                         cfg::network_block::kSig_HttpClient_processRequest,
                                         HttpClientProcessRequestHook,
                                         "HttpClient_processRequest"),
        hook_manager_.RegisterInlineHook(addr_curl_easy_setopt_,
                                         profile->network.curl_easy_setopt,
                                         cfg::network_block::kSig_Curl_easy_setopt,
                                         CurlEasySetoptHook,
                                         "curl_easy_setopt",
                                         true),
    };
    const bool registered = std::ranges::all_of(registrations, [](const auto &registration) {
        return static_cast<bool>(registration);
    });
    const bool installed = registered &&
                           hook_manager_.CommitInlineHook(
                               std::span<HookManager::InlineHookRegistration>(registrations));
    hooks_installed_.store(installed, std::memory_order_release);
    if (!installed) {
        ARC_LOGE("Hook installation incomplete");
    }
    return hooks_installed_;
}

NetworkManager::DispatchResult NetworkManager::DispatchHandlers(HandlerArgs &args) {
    DispatchResult result{};
    const auto snapshot = std::atomic_load_explicit(&handler_snapshot_, std::memory_order_acquire);
    if (!snapshot) return result;

    for (const auto &entry : snapshot->Entries()) {
        if (!entry.fn) continue;

        HandlerArgs candidate = args;
        PopulateBodyViews(candidate);
        const bool modified = entry.fn(candidate);
        if (!modified) continue;

        NormalizeHandlerStrings(candidate);
        args = candidate;
        CopyUrlPath(args.url, args.url_path, sizeof(args.url_path));
        result.modified = true;
        if (args.blocked) {
            result.blocked = true;
            break;
        }
    }

    return result;
}

void NetworkManager::PopulateBodyViews(HandlerArgs &args) {
    args.request_body_view = ReadRequestBodyView(args, cfg::network_block::kNetworkBodyCaptureMax);
    args.response_body_view = ReadResponseBodyView(args, cfg::network_block::kNetworkBodyCaptureMax);
    args.curl_error[0] = '\0';
    if (!args.curl_error_buf ||
        !mem::ProcMaps::IsReadable(reinterpret_cast<uintptr_t>(args.curl_error_buf), 1)) {
        return;
    }
    const size_t length = SafeStrnlen(args.curl_error_buf, cfg::network_block::kCurlErrorMaxLen);
    std::memcpy(args.curl_error, args.curl_error_buf, length);
    args.curl_error[length] = '\0';
}

void NetworkManager::WriteCurlError(char *curl_error_buf, const char *msg) {
    if (!curl_error_buf ||
        !mem::ProcMaps::IsWritable(reinterpret_cast<uintptr_t>(curl_error_buf),
                                    cfg::network_block::kCurlErrorMaxLen + 1)) {
        return;
    }
    if (!msg) {
        curl_error_buf[0] = '\0';
        return;
    }

    const size_t n = SafeStrnlen(msg, cfg::network_block::kCurlErrorMaxLen);
    std::memcpy(curl_error_buf, msg, n);
    curl_error_buf[n] = '\0';
}

uint32_t NetworkManager::CurlEasySetoptHook(uintptr_t curl_handle, uint32_t option, uintptr_t param) {
    auto &manager = Instance();

    const bool tracked_option = option == cfg::network_block::kCurlOpt_ErrorBuffer ||
                                option == cfg::network_block::kCurlOpt_WriteData ||
                                option == cfg::network_block::kCurlOpt_URL;
    if (!tracked_option || !BindTrackedCurlHandle(t_req_ctx, curl_handle)) {
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    if (option == cfg::network_block::kCurlOpt_ErrorBuffer) {
        t_req_ctx.curl_error_buf = reinterpret_cast<char *>(param);
        if (t_req_ctx.curl_error_buf &&
            mem::ProcMaps::IsWritable(reinterpret_cast<uintptr_t>(t_req_ctx.curl_error_buf),
                                      cfg::network_block::kCurlErrorMaxLen + 1)) {
            t_req_ctx.curl_error_buf[0] = '\0';
        }
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    if (option == cfg::network_block::kCurlOpt_WriteData) {
        t_req_ctx.response_body_vec = param;
        return CALL_ORIG(CurlEasySetoptHook, curl_handle, option, param);
    }

    t_req_ctx.sequence = NextSequence(manager.next_sequence_);
    if (param == 0 || !mem::ProcMaps::IsReadable(param, 1)) {
        t_req_ctx.url[0] = '\0';
        t_req_ctx.url_path[0] = '\0';
        t_req_ctx.url_truncated = false;
    } else {
        t_req_ctx.url_truncated = CopyAndSanitizeUrl(
            reinterpret_cast<const char *>(param), t_req_ctx.url, sizeof(t_req_ctx.url));
        CopyUrlPath(t_req_ctx.url, t_req_ctx.url_path, sizeof(t_req_ctx.url_path));
    }

    HandlerArgs args = t_req_ctx.ToHandlerArgs(Phase::BeforeRequest);
    const DispatchResult dispatch = manager.DispatchHandlers(args);

    uintptr_t effective_param = param;
    if (dispatch.modified) {
        t_req_ctx.ApplyFromHandlerArgs(args);
        CopyUrlPath(t_req_ctx.url, t_req_ctx.url_path, sizeof(t_req_ctx.url_path));

        // URL argument can be rewritten by handlers. Always use the context URL
        // when a modification was accepted so next-stage behavior matches args.
        effective_param = reinterpret_cast<uintptr_t>(t_req_ctx.url);
    }

    if (dispatch.blocked) {
        const char *reason = args.block_reason[0] ? args.block_reason : "ArcHelper: blocked";
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
        (void)ReadObject(http_response,
                         cfg::network_block::kHttpResponse_status_code_i64_off,
                         t_req_ctx.response_status_code);
        (void)ReadObject(http_response,
                         cfg::network_block::kHttpResponse_succeed_u8_off,
                         t_req_ctx.response_ok);
    }

    HandlerArgs args = t_req_ctx.ToHandlerArgs(Phase::AfterRequest);
    const DispatchResult dispatch = Instance().DispatchHandlers(args);
    if (dispatch.modified) {
        t_req_ctx.ApplyFromHandlerArgs(args);

        // Apply editable response metadata back to HttpResponse.
        if (http_response) {
            (void)WriteObject(http_response,
                              cfg::network_block::kHttpResponse_status_code_i64_off,
                              args.response_status_code);
            (void)WriteObject(http_response,
                              cfg::network_block::kHttpResponse_succeed_u8_off,
                              args.response_ok);
        }
    }

    t_req_ctx = prev;
    return ret;
}

} // namespace arc_helper
