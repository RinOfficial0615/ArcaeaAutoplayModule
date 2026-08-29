#include "manager/CustomChartManager.hpp"

#include <ranges>
#include <utility>

#include "manager/custom_chart/CustomChartImporter.hpp"
#include "utils/Log.h"

namespace arc_helper {

CustomChartManager &CustomChartManager::Instance() {
    static CustomChartManager manager;
    return manager;
}

bool CustomChartManager::EnsureImported(const CustomChartSettings &settings) {
    if (imported_) return true;
    if (settings.root_dir.empty() || settings.charts_dir.empty() || settings.cache_dir.empty()) {
        ARC_LOGE("Importer paths unavailable");
        return false;
    }

    CustomChartImporter importer(settings);
    auto result = importer.Import();
    if (!result) {
        ARC_LOGE("Importer failed: %s", result.error().c_str());
        return false;
    }

    // Publish only after importer and report writer have completed.  A
    // failed retry therefore cannot expose a half-built snapshot.
    snapshot_ = std::move(*result);
    imported_ = true;
    ARC_LOGI("songs=%zu assets=%zu state=READY",
             snapshot_.SongCount(), snapshot_.AssetCount());
    return true;
}

std::string CustomChartManager::MergeSonglist(std::string_view official_json,
                                              std::string &error) const {
    return snapshot_.MergeOfficialSonglist(official_json, error);
}

const std::string *CustomChartManager::ResolveAsset(std::string_view game_path) const {
    return snapshot_.assets.Resolve(game_path);
}

bool CustomChartManager::ContainsSongId(std::string_view song_id) const {
    return snapshot_.assets.ContainsSongId(song_id);
}

std::vector<std::string> CustomChartManager::ListAssetDirectory(std::string_view game_path) const {
    return snapshot_.assets.ListDirectory(game_path);
}

std::vector<std::string> CustomChartManager::ListSongIdsForDifficulty(int difficulty) const {
    // in_range rejects a negative difficulty and anything that will not fit in
    // a size_t in one step, so the slot index needs no further guarding.
    if (!std::in_range<size_t>(difficulty) ||
        static_cast<size_t>(difficulty) >= cfg::custom_charts::kDifficultyCount) {
        return {};
    }
    const size_t slot = static_cast<size_t>(difficulty);
    return snapshot_.songs |
           std::views::filter([slot](const auto &song) { return song.has_chart[slot]; }) |
           std::views::transform([](const auto &song) { return song.id; }) |
           std::ranges::to<std::vector>();
}

bool CustomChartManager::IsCustomChartPath(std::string_view game_path,
                                           std::string *song_id) const {
    return snapshot_.assets.IsCustomChartPath(game_path, song_id);
}

} // namespace arc_helper
