#pragma once

namespace arc_autoplay::cfg::module {

// Shared module identity.
inline constexpr const char *kLogTag = "ArcAutoplay";
inline constexpr const char *kRuntimeClass = "java/lang/Runtime";
inline constexpr const char *kLibName = "libcocos2dcpp.so";

// Package allowlist used by the Zygisk entry wrapper.
inline constexpr const char *kTargetPackages[] = {
    "moe.inf.arc",
    "moe.low.arc",
    "moe.low.mes",
};

// Compile-time feature switches.
inline constexpr bool kAutoplayEnabled = true;
inline constexpr bool kNetworkLoggerEnabled = true;
inline constexpr bool kNetworkBlockEnabled = true;
inline constexpr bool kDisableSslPinsEnabled = false;

} // namespace arc_autoplay::cfg::module
