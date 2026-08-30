#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

namespace arc_helper::cfg {

enum class GameVersionId : uint8_t {
    kUnknown = 0,
    k61211c,
    k6132f,
    k6140c,
    k6162c,
    k6168c,
    k7000c,
    k7001c,
};

// Layout selection at runtime. Every build through 6.16.8c shares one set of
// note-family offsets; 7.0.x shifts the derived tail, so consumers must ask
// which row applies. Features install this right before arming hooks; until
// then reads fall back to the shared legacy row.
inline constinit std::atomic<GameVersionId> runtime_layout_version{GameVersionId::kUnknown};

inline void SetRuntimeLayoutVersion(GameVersionId version) {
    runtime_layout_version.store(version, std::memory_order_relaxed);
}

inline GameVersionId GetRuntimeLayoutVersion() {
    const GameVersionId version = runtime_layout_version.load(std::memory_order_relaxed);
    return version == GameVersionId::kUnknown ? GameVersionId::k61211c : version;
}

struct VersionProbeOffsets {
    uintptr_t app_version_string = 0;
};

struct AutoplayOffsets {
    uintptr_t gameplay_process_logic_notes = 0;
    uintptr_t gameplay_try_tap_judgement_for_touch = 0;
    uintptr_t score_state_apply_judgement = 0;
    uintptr_t score_state_apply_miss = 0;
    uintptr_t show_judgement_effect_at_note = 0;
    uintptr_t note_effect_on_miss = 0;
    uintptr_t note_effect_on_judgement = 0;
    uintptr_t logic_color_accepts_touch = 0;
    uintptr_t patch_process_logic_notes_add64_a = 0;
    uintptr_t patch_process_logic_notes_add64_b = 0;
    uintptr_t patch_process_logic_notes_addc8 = 0;
    uintptr_t typeinfo_logic_hold_note = 0;
    uintptr_t typeinfo_logic_arc_note = 0;
};

struct NetworkOffsets {
    uintptr_t httpclient_process_request = 0;
    uintptr_t curl_easy_setopt = 0;
};

struct SslPinsOffsets {
    uintptr_t skip_cbz = 0;     // CBZ X20,skip → B skip
    uintptr_t tail_call = 0;    // BL sub_XXXX → NOP
    uint32_t expected_skip_cbz = 0;
    uint32_t expected_tail_call = 0;
};

struct CustomChartsOffsets {
    uintptr_t songlist_parser = 0;
    // Return address immediately after the validated AAssetManager_open BL
    // that reads songs/songlist (6.16.2c: 0x142CFB0, 6.16.8c: 0x1709D98,
    // 7.0.0c: 0x105F0F8, 7.0.1c: 0xE22208).
    // This is deliberately an exact caller match; the nearby integrity/preload
    // caller is official.
    uintptr_t songlist_asset_loader_caller = 0;
    uintptr_t asset_bundle_loader = 0;
    uintptr_t songlist_digest_size_guard = 0;
    uintptr_t songlist_digest_compare_guard = 0;
    uintptr_t songlist_difficulty_filter = 0;
    uintptr_t difficulty_availability = 0;
    uintptr_t song_unlock_mask_check = 0;
    uintptr_t content_availability = 0;
    uintptr_t play_launcher = 0;
    uintptr_t chart_path = 0;
    uintptr_t song_registry_global = 0;
    uintptr_t find_song_by_id = 0;
    // Encoded BL at songlist_asset_loader_caller - 4; the immediate differs
    // per build because it targets the PLT stub of that binary.
    uint32_t expected_songlist_loader_call = 0;
    // IDA sub_E6F768: bool getter over the play-context byte +0x110. Only the
    // retired April-Fools dynamix_conflict chart raises that byte, so parsed
    // scenecontrol/timing commands stay dormant for every other chart. The
    // module swaps this function for an always-true return while custom charts
    // are installed; zero keeps older profiles untouched.
    uintptr_t scenecontrol_gate_getter = 0;
};

struct FeatureCapabilities {
    bool autoplay = false;
    bool network = false;
    bool custom_charts = false;
};

struct GameProfile {
    GameVersionId id = GameVersionId::kUnknown;
    const char *version_name = nullptr;
    VersionProbeOffsets version_probe{};
    AutoplayOffsets autoplay{};
    NetworkOffsets network{};
    SslPinsOffsets ssl_pins{};
    CustomChartsOffsets custom_charts{};
    FeatureCapabilities capabilities{};
};

inline constexpr std::array<GameProfile, 7> kSupportedGameProfiles = {{
    {
        .id = GameVersionId::k61211c,
        .version_name = "6.12.11c",
        .version_probe = {
            .app_version_string = 0x1918CD0,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0x147671C,
            .gameplay_try_tap_judgement_for_touch = 0x134BDA8,
            .score_state_apply_judgement = 0x0ACEEF4,
            .score_state_apply_miss = 0x0E532B0,
            .show_judgement_effect_at_note = 0x1253E98,
            .note_effect_on_miss = 0x1547754,
            .note_effect_on_judgement = 0x15D3180,
            .logic_color_accepts_touch = 0x116BA0C,
            .patch_process_logic_notes_add64_a = 0x1476C04,
            .patch_process_logic_notes_add64_b = 0x1476CBC,
            .patch_process_logic_notes_addc8 = 0x1476D0C,
            .typeinfo_logic_hold_note = 0x18AAB60,
            .typeinfo_logic_arc_note = 0x18265D0,
        },
        .network = {
            .httpclient_process_request = 0x133E000,
            .curl_easy_setopt = 0x0C2F838,
        },
        .ssl_pins = {
            .skip_cbz = 0xBBCB24,
            .tail_call = 0xBBF3F0,
            .expected_skip_cbz = 0xB40000F4,
            .expected_tail_call = 0x97F866C5,
        },
        .capabilities = {.autoplay = true, .network = true},
    },
    {
        .id = GameVersionId::k6132f,
        .version_name = "6.13.2f",
        .version_probe = {
            .app_version_string = 0x1872370,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0x0E823CC,
            .gameplay_try_tap_judgement_for_touch = 0x13E9414,
            .score_state_apply_judgement = 0x0EC79B4,
            .score_state_apply_miss = 0x0E30F20,
            .show_judgement_effect_at_note = 0x122BB88,
            .note_effect_on_miss = 0x0A29080,
            .note_effect_on_judgement = 0x0E6F10C,
            .logic_color_accepts_touch = 0x09E8A70,
            .patch_process_logic_notes_add64_a = 0x0E828B4,
            .patch_process_logic_notes_add64_b = 0x0E8296C,
            .patch_process_logic_notes_addc8 = 0x0E829BC,
            .typeinfo_logic_hold_note = 0x1734438,
            .typeinfo_logic_arc_note = 0x17D4F80,
        },
        .network = {
            .httpclient_process_request = 0x1550024,
            .curl_easy_setopt = 0x0D8FD08,
        },
        .ssl_pins = {},
        .capabilities = {.autoplay = true, .network = true},
    },
    {
        .id = GameVersionId::k6140c,
        .version_name = "6.14.0c",
        .version_probe = {
            .app_version_string = 0x1984120,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0x1511C64,
            .gameplay_try_tap_judgement_for_touch = 0x8282E8,
            .score_state_apply_judgement = 0x97332C,
            .score_state_apply_miss = 0x179222C,
            .show_judgement_effect_at_note = 0x89D48C,
            .note_effect_on_miss = 0x1363C88,
            .note_effect_on_judgement = 0xDE4060,
            .logic_color_accepts_touch = 0xEA9488,
            .patch_process_logic_notes_add64_a = 0x151214C,
            .patch_process_logic_notes_add64_b = 0x1512204,
            .patch_process_logic_notes_addc8 = 0x1512254,
            .typeinfo_logic_hold_note = 0x18960F0,
            .typeinfo_logic_arc_note = 0x1891500,
        },
        .network = {
            .httpclient_process_request = 0x127FFE4,
            .curl_easy_setopt = 0xFC8250,
        },
        .ssl_pins = {
            .skip_cbz = 0x10D462C,
            .tail_call = 0x10D6EF8,
            .expected_skip_cbz = 0xB40000F4,
            .expected_tail_call = 0x97E6365C,
        },
        .capabilities = {.autoplay = true, .network = true},
    },
    {
        .id = GameVersionId::k6162c,
        .version_name = "6.16.2c",
        .version_probe = {
            .app_version_string = 0x1A9C880,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0xBEE260,
            .gameplay_try_tap_judgement_for_touch = 0x184FBD4,
            .score_state_apply_judgement = 0x745BC4,
            .score_state_apply_miss = 0x1361DE4,
            .show_judgement_effect_at_note = 0x103A07C,
            .note_effect_on_miss = 0x14A948C,
            .note_effect_on_judgement = 0xA193CC,
            .logic_color_accepts_touch = 0xE59C90,
            .patch_process_logic_notes_add64_a = 0xBEE748,
            .patch_process_logic_notes_add64_b = 0xBEE800,
            .patch_process_logic_notes_addc8 = 0xBEE850,
            .typeinfo_logic_hold_note = 0x1979468,
            .typeinfo_logic_arc_note = 0x1A10D78,
        },
        .network = {
            .httpclient_process_request = 0x17E9698,
            .curl_easy_setopt = 0x778FD0,
        },
        .ssl_pins = {},
        .custom_charts = {
            .songlist_parser = 0xCA2280,
            .songlist_asset_loader_caller = 0x142CFB0,
            .asset_bundle_loader = 0x100F3E8,
            .songlist_digest_size_guard = 0x100F814,
            .songlist_digest_compare_guard = 0x100F830,
            .songlist_difficulty_filter = 0x12638FC,
            .difficulty_availability = 0xE162C8,
            .song_unlock_mask_check = 0xD988F4,
            .content_availability = 0x121E9FC,
            .play_launcher = 0xC987A8,
            .chart_path = 0xA74680,
            .song_registry_global = 0x1AAB6E0,
            .find_song_by_id = 0xCADFA4,
            .expected_songlist_loader_call = 0x94140479,
        },
        .capabilities = {.autoplay = true, .network = true, .custom_charts = true},
    },
    {
        .id = GameVersionId::k6168c,
        .version_name = "6.16.8c",
        .version_probe = {
            .app_version_string = 0x1AA3250,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0xD7A8D4,
            .gameplay_try_tap_judgement_for_touch = 0xD4F330,
            .score_state_apply_judgement = 0xDA6D04,
            .score_state_apply_miss = 0x979CD0,
            .show_judgement_effect_at_note = 0x1401F98,
            .note_effect_on_miss = 0xB79F1C,
            .note_effect_on_judgement = 0xB4D628,
            .logic_color_accepts_touch = 0x11F1AC4,
            .patch_process_logic_notes_add64_a = 0xD7ADBC,
            .patch_process_logic_notes_add64_b = 0xD7AE74,
            .patch_process_logic_notes_addc8 = 0xD7AEC4,
            .typeinfo_logic_hold_note = 0x196A440,
            .typeinfo_logic_arc_note = 0x19549E0,
        },
        .network = {
            .httpclient_process_request = 0xFA5D40,
            .curl_easy_setopt = 0x750734,
        },
        .ssl_pins = {},
        .custom_charts = {
            .songlist_parser = 0xDC9CBC,
            .songlist_asset_loader_caller = 0x1709D98,
            .asset_bundle_loader = 0x77AA00,
            .songlist_digest_size_guard = 0x77AE2C,
            .songlist_digest_compare_guard = 0x77AE48,
            .songlist_difficulty_filter = 0xD3AF00,
            .difficulty_availability = 0xE8792C,
            .song_unlock_mask_check = 0x1068020,
            .content_availability = 0xF64A90,
            .play_launcher = 0xA1F1A0,
            .chart_path = 0xDA71E8,
            .song_registry_global = 0x1A9DA58,
            .find_song_by_id = 0xC05E74,
            .expected_songlist_loader_call = 0x94089DEB,
        },
        .capabilities = {.autoplay = true, .network = true, .custom_charts = true},
    },
    {
        .id = GameVersionId::k7000c,
        .version_name = "7.0.0c",
        .version_probe = {
            .app_version_string = 0x1BAF300,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0xF21224,
            .gameplay_try_tap_judgement_for_touch = 0x109F144,
            .score_state_apply_judgement = 0x9D5500,
            .score_state_apply_miss = 0xE03F70,
            .show_judgement_effect_at_note = 0xB804E4,
            .note_effect_on_miss = 0x7A5484,
            .note_effect_on_judgement = 0xD0B0F4,
            .logic_color_accepts_touch = 0x18DBD7C,
            .patch_process_logic_notes_add64_a = 0xF2170C,
            .patch_process_logic_notes_add64_b = 0xF217C4,
            .patch_process_logic_notes_addc8 = 0xF21814,
            .typeinfo_logic_hold_note = 0x1A54038,
            .typeinfo_logic_arc_note = 0x1B08D30,
        },
        .network = {
            .httpclient_process_request = 0x1459B6C,
            .curl_easy_setopt = 0xD8B4C4,
        },
        .ssl_pins = {},
        .custom_charts = {
            .songlist_parser = 0xCCE050,
            .songlist_asset_loader_caller = 0x105F0F8,
            .asset_bundle_loader = 0x928590,
            .songlist_digest_size_guard = 0x9289BC,
            .songlist_digest_compare_guard = 0x9289D8,
            .songlist_difficulty_filter = 0xB6E964,
            .difficulty_availability = 0x195AE9C,
            .song_unlock_mask_check = 0x1399B04,
            .content_availability = 0x18D8DE0,
            .play_launcher = 0xA0C430,
            .chart_path = 0xF4D3C8,
            .song_registry_global = 0x1BC28F0,
            .find_song_by_id = 0x7AF94C,
            .expected_songlist_loader_call = 0x94275287,
            .scenecontrol_gate_getter = 0xE6F768,
        },
        .capabilities = {.autoplay = true, .network = true, .custom_charts = true},
    },
    {
        .id = GameVersionId::k7001c,
        .version_name = "7.0.1c",
        .version_probe = {
            .app_version_string = 0x1BAA8C8,
        },
        .autoplay = {
            .gameplay_process_logic_notes = 0xFFC84C,
            .gameplay_try_tap_judgement_for_touch = 0x15D28B0,
            .score_state_apply_judgement = 0x140CFF4,
            .score_state_apply_miss = 0x11306E0,
            .show_judgement_effect_at_note = 0xAC4794,
            .note_effect_on_miss = 0x8372E0,
            .note_effect_on_judgement = 0x19AE168,
            .logic_color_accepts_touch = 0x886444,
            .patch_process_logic_notes_add64_a = 0xFFCD34,
            .patch_process_logic_notes_add64_b = 0xFFCDEC,
            .patch_process_logic_notes_addc8 = 0xFFCE3C,
            .typeinfo_logic_hold_note = 0x1AA4C68,
            .typeinfo_logic_arc_note = 0x1B0DAC8,
        },
        .network = {
            .httpclient_process_request = 0x10FC860,
            .curl_easy_setopt = 0x1A09024,
        },
        .ssl_pins = {},
        .custom_charts = {
            .songlist_parser = 0xC8B1D0,
            .songlist_asset_loader_caller = 0xE22208,
            .asset_bundle_loader = 0x17BBBD8,
            .songlist_digest_size_guard = 0x17BC004,
            .songlist_digest_compare_guard = 0x17BC020,
            .songlist_difficulty_filter = 0x18AEFBC,
            .difficulty_availability = 0x1638058,
            .song_unlock_mask_check = 0x11A0070,
            .content_availability = 0x19A7194,
            .play_launcher = 0x18233EC,
            .chart_path = 0x1937B3C,
            .song_registry_global = 0x1BAEB28,
            .find_song_by_id = 0xE4B5F8,
            .expected_songlist_loader_call = 0x9430485F,
            .scenecontrol_gate_getter = 0x11A57A8,
        },
        .capabilities = {.autoplay = true, .network = true, .custom_charts = true},
    },
}};

consteval bool GameProfilesCoverKnownVersions() {
    for (const GameVersionId version : magic_enum::enum_values<GameVersionId>()) {
        // count_if returns a signed difference type, so the expected count must
        // be signed too or -Wsign-compare rejects the comparison.
        const auto matches = std::ranges::count_if(
            kSupportedGameProfiles, [version](const auto &profile) { return profile.id == version; });
        if (matches != (version == GameVersionId::kUnknown ? 0 : 1)) return false;
    }
    return true;
}

static_assert(GameProfilesCoverKnownVersions(),
              "each known game version must have exactly one profile");

// Either pointer may legitimately be null: an unread version string, or a
// profile that carries no name. Building a string_view from nullptr is UB, so
// the pointers are checked before being viewed; the comparison itself is then
// a plain value compare rather than a byte scan.
inline bool GameVersionMatches(const char *actual, const char *expected) {
    return actual && expected && std::string_view(actual) == std::string_view(expected);
}

inline const GameProfile *FindGameProfileByVersionString(const char *version) {
    const auto it = std::ranges::find_if(kSupportedGameProfiles, [version](const auto &profile) {
        return GameVersionMatches(version, profile.version_name);
    });
    return it != kSupportedGameProfiles.end() ? &(*it) : nullptr;
}

} // namespace arc_helper::cfg
