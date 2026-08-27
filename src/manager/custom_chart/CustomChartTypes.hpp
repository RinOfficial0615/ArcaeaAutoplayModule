#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "config/CustomChartConfig.h"

namespace arc_helper {

struct ImportedChart {
    int slot = -1;
    std::string chart_path;
    std::string source_name;
    std::string charter;
    std::string jacket_designer;
    int rating = 0;
    bool rating_plus = false;
    // Display class echoed from songlist metadata (`ratingClassAlias`). The
    // engine keeps the runtime slot from `ratingClass`; a nonzero value only
    // re-styles it, e.g. slot 3 renders as Inscribed instead of Beyond.
    int rating_class_alias = 0;
};

struct ImportedSong {
    std::string id;
    std::string source_id;
    std::string title;
    std::string artist;
    std::string bpm;
    double bpm_base = 0.0;
    int side = 0;
    std::string bg;
    std::string bg_path;
    int64_t preview_start = 0;
    int64_t preview_end = 0;
    std::string audio_path;
    std::string jacket_path;
    std::string jacket_256_path;
    std::array<ImportedChart, cfg::custom_charts::kDifficultyCount> charts{};
    std::array<bool, cfg::custom_charts::kDifficultyCount> has_chart{};
};

struct CustomChartSettings {
    std::string root_dir;
    std::string charts_dir;
    std::string cache_dir;
    std::string default_artist;
    std::string default_designer;
    double default_bpm = 0.0;
    int default_side = 0;
    std::string default_background;
    int64_t default_preview_start_ms = 0;
    int64_t default_preview_duration_ms = 0;
    int default_chart_difficulty = 0;
    int default_rating = 0;
    std::string fallback_song_id;
    int rating_plus_minimum_rating = 0;
    double rating_plus_threshold = 0.0;
    std::optional<int> override_side;
    std::optional<std::string> override_background;
};

struct ImportDiagnostic {
    std::string package;
    std::string item;
    std::string status;
    std::string detail;
};

} // namespace arc_helper
