#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace arc_autoplay::cfg {

enum class GameVersionId : uint8_t {
    kUnknown = 0,
    k61211c,
    k6132f,
    k6140c,
};

struct VersionProbeOffsets {
    uintptr_t set_app_version = 0;
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
};

struct GameProfile {
    GameVersionId id = GameVersionId::kUnknown;
    const char *version_name = nullptr;
    VersionProbeOffsets version_probe{};
    AutoplayOffsets autoplay{};
    NetworkOffsets network{};
    SslPinsOffsets ssl_pins{};
};

inline constexpr std::array<GameProfile, 3> kSupportedGameProfiles = {{
    {
        .id = GameVersionId::k61211c,
        .version_name = "6.12.11c",
        .version_probe = {
            .set_app_version = 0xDDD160,
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
        },
    },
    {
        .id = GameVersionId::k6132f,
        .version_name = "6.13.2f",
        .version_probe = {
            .set_app_version = 0xBD57DC,
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
    },
    {
        .id = GameVersionId::k6140c,
        .version_name = "6.14.0c",
        .version_probe = {
            .set_app_version = 0x89F528,
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
        },
    },
}};

inline bool GameVersionMatches(const char *actual, const char *expected) {
    return actual && expected && std::strcmp(actual, expected) == 0;
}

inline const GameProfile *FindGameProfileByVersionString(const char *version) {
    for (const auto &profile : kSupportedGameProfiles) {
        if (profile.version_name && GameVersionMatches(version, profile.version_name)) {
            return &profile;
        }
    }
    return nullptr;
}

} // namespace arc_autoplay::cfg
