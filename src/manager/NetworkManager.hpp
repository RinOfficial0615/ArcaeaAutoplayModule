#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config/NetworkBlockConfig.h"
#include "manager/HookManager.hpp"
#include "manager/network/NetworkHandler.hpp"

namespace arc_autoplay {

// Intercepts network requests and dispatches them to registered handlers.
//
// Responsibilities:
// - Install and own low-level network hooks.
// - Build mutable handler args from intercepted request state.
// - Execute handlers by priority (high -> low).
// - Stop handler execution when a handler marks the request blocked.
// - Apply accepted handler modifications back to hook behavior.
class NetworkManager {
public:
    using Phase = network::Phase;
    using HandlerArgs = network::HandlerArgs;
    using HandlerFn = network::HandlerFn;
    using BufferViewStatus = network::BufferViewStatus;
    using BufferView = network::BufferView;

    static NetworkManager &Instance();

    // Register one handler. Higher priority runs earlier.
    bool RegisterHandler(const char *name, int priority, HandlerFn fn);

    // Utilities available to handlers.
    static const char *HttpMethodStr(uint32_t request_type);
    static void CopyAndSanitizeUrl(const char *url, char *out, size_t out_size);
    static std::string EscapeBytesForLog(const uint8_t *data, size_t len);
    static BufferView ReadRequestBodyView(const HandlerArgs &args, size_t max_bytes);
    static BufferView ReadResponseBodyView(const HandlerArgs &args, size_t max_bytes);

private:
    using ActiveRequestCtx = network::ActiveRequestCtx;
    using CurlEasySetoptFn = uint32_t (*)(uintptr_t curl_handle, uint32_t option, uintptr_t param);
    using HttpClientProcessRequestFn = int64_t (*)(uintptr_t http_client, uintptr_t http_response, char *curl_error_buf);

    struct HandlerEntry {
        const char *name = nullptr;
        int priority = 0;
        uint32_t register_order = 0;
        HandlerFn fn = nullptr;
    };

    struct DispatchResult {
        bool modified = false;
        bool blocked = false;
    };

    NetworkManager() = default;

    bool EnsureHooksInstalled();
    DispatchResult DispatchHandlers(HandlerArgs &args);

    static uint32_t CurlEasySetoptHook(uintptr_t curl_handle, uint32_t option, uintptr_t param);
    static int64_t HttpClientProcessRequestHook(uintptr_t http_client, uintptr_t http_response, char *curl_error_buf);

    static void WriteCurlError(char *curl_error_buf, const char *msg);

    HookManager &hook_manager_ = HookManager::Instance();
    uintptr_t lib_base_ = 0;

    uintptr_t addr_curl_easy_setopt_ = 0;

    uintptr_t addr_httpclient_process_request_ = 0;

    std::vector<HandlerEntry> handlers_{};
    uint32_t next_register_order_ = 0;
    uint32_t next_sequence_ = 0;
    bool hooks_installed_ = false;
};

} // namespace arc_autoplay
