#include "manager/custom_chart/CustomChartSnapshot.hpp"

#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

#include "config/CustomChartConfig.h"

namespace arc_helper {
namespace {

using Json = nlohmann::json;

} // namespace

std::string ImportSnapshot::SongsJson() const {
    Json result = Json::array();
    for (const auto &song : songs) {
        Json item = {
            {"id", song.id},
            {"title_localized", {{"en", song.title}}},
            {"artist", song.artist},
            {"bpm", song.bpm},
            {"bpm_base", song.bpm_base},
            {"set", cfg::custom_charts::kSongSet},
            {"purchase", cfg::custom_charts::kSongPurchase},
            {"audioPreview", song.preview_start},
            {"audioPreviewEnd", song.preview_end},
            {"side", song.side},
            {"bg", song.bg},
            {"date", cfg::custom_charts::kSongDate},
            {"version", cfg::custom_charts::kSongVersion},
            {"difficulties", Json::array()},
        };
        for (size_t slot = 0; slot < cfg::custom_charts::kDifficultyCount; ++slot) {
            const bool present = song.has_chart[slot];
            const bool placeholder = !present && slot <= cfg::custom_charts::kFutureDifficulty;
            if (!present && !placeholder) continue;
            Json difficulty = {
                {"ratingClass", slot},
                {"chartDesigner", present ? song.charts[slot].charter : std::string{}},
                {"jacketDesigner", present ? song.charts[slot].jacket_designer : std::string{}},
                {"rating", present ? song.charts[slot].rating
                                   : cfg::custom_charts::kPlaceholderRating},
            };
            if (present && song.charts[slot].rating_plus) difficulty["ratingPlus"] = true;
            // The engine derives the runtime slot from ratingClass alone
            // (`sub_1194380`); ratingClassAlias only restyles the entry, so it
            // is emitted solely when metadata carried one (Inscribed).
            if (present && song.charts[slot].rating_class_alias > 0) {
                difficulty["ratingClassAlias"] = song.charts[slot].rating_class_alias;
            }
            item["difficulties"].push_back(std::move(difficulty));
        }
        // The 6.16.2c parser treats Beyond as locally locked unless this
        // song-level flag is present. Custom charts are local assets, so a
        // song with a Beyond chart must opt into the same local-unlock path.
        if (song.has_chart[cfg::custom_charts::kBeyondDifficulty]) {
            item["byd_local_unlock"] = true;
        }
        result.push_back(std::move(item));
    }
    return result.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string ImportSnapshot::MergeOfficialSonglist(std::string_view official_json,
                                                  std::string &error) const {
    error.clear();
    Json merged = Json::parse(official_json, nullptr, false);
    if (merged.is_discarded() || !merged.is_object()) {
        error = "official songlist is invalid";
        return {};
    }
    auto songs_value = merged.find("songs");
    if (songs_value == merged.end() || !songs_value->is_array()) {
        error = "songs array missing";
        return {};
    }
    Json custom = Json::parse(SongsJson(), nullptr, false);
    if (custom.is_discarded() || !custom.is_array()) {
        error = "generated custom songs are invalid";
        return {};
    }

    int64_t next_idx = 0;
    for (const auto &song : *songs_value) {
        if (!song.is_object()) continue;
        const auto idx = song.find("idx");
        if (idx == song.end() || !idx->is_number_integer()) continue;
        const int64_t value = idx->get<int64_t>();
        if (value == std::numeric_limits<int64_t>::max()) {
            error = "official songlist index exhausted";
            return {};
        }
        if (value >= next_idx) next_idx = value + 1;
    }
    for (auto &song : custom) {
        if (next_idx == std::numeric_limits<int64_t>::max()) {
            error = "custom songlist index exhausted";
            return {};
        }
        song["idx"] = next_idx++;
    }
    songs_value->insert(songs_value->end(), custom.begin(), custom.end());
    return merged.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace arc_helper
