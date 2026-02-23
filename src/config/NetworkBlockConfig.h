#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace arc_autoplay::cfg::network_block {

// Address constants for network hook points in `libcocos2dcpp.so`.
//
// Hooks are resolved by 16-byte signatures (preferred), with offsets used as a
// fast validation hint.

// HttpClient worker per-request processing (called on network thread).
inline constexpr uintptr_t kLibcocos2dcpp_HttpClient_processRequest = 0x133E000;

// curl_easy_setopt-like wrapper used throughout HttpClient worker path.
inline constexpr uintptr_t kLibcocos2dcpp_Curl_easy_setopt = 0x0C2F838;

// Return values used by NetworkBlock hook.
inline constexpr uint32_t kCurlSetoptRetBlocked = 0xB10Cu; // dedicated non-zero marker
inline constexpr uint32_t kCurlSetoptRetNoOrig = 1u;       // conservative fallback error

// Handler priorities (larger value runs earlier).
inline constexpr int kHandlerPriorityNetworkLogger = 100;
inline constexpr int kHandlerPriorityNetworkBlock = -100;

// Signatures (first 16 bytes).

inline constexpr std::array<uint8_t, 16> kSig_HttpClient_processRequest = {
    0xFF, 0x83, 0x01, 0xD1,
    0xFD, 0x7B, 0x02, 0xA9,
    0xF7, 0x1B, 0x00, 0xF9,
    0xF6, 0x57, 0x04, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSig_Curl_easy_setopt = {
    0xFF, 0x03, 0x04, 0xD1,
    0xFD, 0x7B, 0x0F, 0xA9,
    0xFD, 0xC3, 0x03, 0x91,
    0xA2, 0x0F, 0x39, 0xA9,
};

// HttpRequest layout.
// These offsets come from decomp of the HttpClient worker path.
inline constexpr size_t kHttpRequest_type_u32_off = 12;          // 0=GET,1=POST,2=PUT,3=DELETE
inline constexpr size_t kHttpRequest_body_begin_ptr_off = 40;    // begin pointer (POST/PUT)
inline constexpr size_t kHttpRequest_body_end_ptr_off = 48;      // end pointer (POST/PUT)

// HttpResponse layout.
inline constexpr size_t kHttpResponse_request_ptr_off = 16;
inline constexpr size_t kHttpResponse_succeed_u8_off = 24;
inline constexpr size_t kHttpResponse_body_vec_off = 32; // std::vector<char>
inline constexpr size_t kHttpResponse_status_code_i64_off = 80;

// libcurl option ids (for curl_easy_setopt)
inline constexpr uint32_t kCurlOpt_URL = 10002;        // CURLOPT_URL
inline constexpr uint32_t kCurlOpt_WriteData = 10001;  // CURLOPT_WRITEDATA
inline constexpr uint32_t kCurlOpt_WriteFunction = 20011; // CURLOPT_WRITEFUNCTION
inline constexpr uint32_t kCurlOpt_ErrorBuffer = 10010; // CURLOPT_ERRORBUFFER

// Audit logging
inline constexpr bool kAuditLogAllRequests = true;
inline constexpr uint32_t kAuditLogLimit = 200;
inline constexpr bool kAuditStripQuery = true;
inline constexpr size_t kAuditUrlMaxLen = 240;

// Request body logging (WARNING: may contain sensitive data)
inline constexpr bool kAuditLogBody = true;
inline constexpr bool kAuditLogBodyOnlyNonGet = true;
inline constexpr size_t kAuditBodyMaxBytes = 0; // 0 = unlimited

// Response logging (WARNING: may contain sensitive data)
inline constexpr bool kAuditLogResponse = true;
inline constexpr size_t kAuditResponseMaxBytes = 2048; // 0 = unlimited

// Blocking policy
// - Only the block rules below are applied (plus optional global overrides).
// - Optionally hard-block all non-GET (POST/PUT/DELETE) for safe testing.
inline constexpr bool kBlockAllNonGet = false;
inline constexpr bool kBlockAllRequests = false;
inline constexpr uint32_t kBlockedLogLimit = 50;

enum class RuleMatchType : uint8_t {
    PathPrefix,
    PathSuffix,
};

// HTTP method bits mapped from HttpRequest type:
// 0=GET, 1=POST, 2=PUT, 3=DELETE
inline constexpr uint8_t kMethodGet = 1u << 0;
inline constexpr uint8_t kMethodPost = 1u << 1;
inline constexpr uint8_t kMethodPut = 1u << 2;
inline constexpr uint8_t kMethodDelete = 1u << 3;

struct NetworkBlockRule {
    const char *reason;
    uint8_t method_mask;
    RuleMatchType match_type;
    const char *pattern;
};

// Block rules (match by URL postfix/path; API base prefix can change).
inline constexpr std::array<NetworkBlockRule, 8> kBlockRules = {{
    {"world/map/me", kMethodGet | kMethodPost, RuleMatchType::PathPrefix, "/world/map/me"},
    {"score/token/world", kMethodGet, RuleMatchType::PathSuffix, "/score/token/world"},
    {"score/song (POST)", kMethodPost, RuleMatchType::PathSuffix, "/score/song"},
    {"score/token", kMethodGet, RuleMatchType::PathSuffix, "/score/token"},
    {"user/me/save (POST)", kMethodPost, RuleMatchType::PathSuffix, "/user/me/save"},
    {"multiplayer/room/create (POST)", kMethodPost, RuleMatchType::PathSuffix, "/multiplayer/me/room/create"},
    {"multiplayer/matchmaking/join (POST)", kMethodPost, RuleMatchType::PathPrefix, "/multiplayer/me/matchmaking/join"},
    {"multiplayer/matchmaking/status (POST)", kMethodPost, RuleMatchType::PathPrefix, "/multiplayer/me/matchmaking/status"},
}};

} // namespace arc_autoplay::cfg::network_block
