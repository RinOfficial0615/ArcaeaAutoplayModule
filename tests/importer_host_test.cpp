#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "manager/CustomChartManager.hpp"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string root_path = argv[1];
    const arc_helper::CustomChartSettings settings{
        .root_dir = root_path,
        .charts_dir = root_path + "/charts",
        .cache_dir = root_path + "/cache",
        .default_artist = "Configured Artist",
        .default_designer = "Configured Designer",
        .default_bpm = 120.0,
        .default_side = 1,
        .default_background = "base_conflict",
        .default_preview_start_ms = 0,
        .default_preview_duration_ms = 30000,
        .default_chart_difficulty = 2,
        .default_rating = 0,
        .fallback_song_id = "configured",
        .rating_plus_minimum_rating = 7,
        .rating_plus_threshold = 0.69999,
        .override_side = std::nullopt,
        .override_background = std::nullopt,
    };
    auto &manager = arc_helper::CustomChartManager::Instance();
    assert(manager.ImportForTesting(settings));
    const size_t expected_songs = manager.SongCountForTesting();
    assert(expected_songs > 0);
    assert(manager.AssetCountForTesting() >= expected_songs * 4);
    const std::string songs_json = manager.SongsJsonForTesting();
    const auto songs = nlohmann::json::parse(songs_json, nullptr, false);
    assert(songs.is_array());
    // ArcCreate only marks constants >= 7 with a fractional part >= 0.69999
    // as plus. A textual difficulty remains the fallback when chartConstant
    // is absent.
    bool found_text_rating_10_plus = false;
    bool checked_tiny_tales = false;
    bool checked_light_side = false;
    bool checked_eternal = false;
    bool checked_bounded_numeric = false;
    bool checked_bounded_arc = false;
    bool checked_configured_default = false;
    bool checked_plain_songlist = false;
    bool checked_songlist_conflict = false;
    for (const auto &song : songs) {
        assert(song.at("id").get<std::string>().size() <= 21);
        assert(manager.ContainsSongId(song.at("id").get<std::string>()));
        const std::string song_root = "songs/" + song.at("id").get<std::string>();
        const auto directory_entries = manager.ListAssetDirectory(song_root);
        assert(std::ranges::find(directory_entries, "base.ogg") != directory_entries.end());
        assert(std::ranges::find(directory_entries, "base.jpg") != directory_entries.end());
        assert(std::ranges::any_of(directory_entries, [](const auto &entry) {
            return entry.ends_with(".aff");
        }));
        assert(manager.ListAssetDirectory("assets/" + song_root) == directory_entries);
        assert(manager.ListAssetDirectory("Resources/" + song_root + "/") == directory_entries);
        assert(manager.ListAssetDirectory("file:///android_asset/" + song_root) == directory_entries);
        assert(manager.ResolveAsset((song_root + "/1080/base.jpg").c_str()) != nullptr);
        assert(manager.ResolveAsset((song_root + "/1080/base_256.jpg").c_str()) != nullptr);
        assert(manager.ResolveAsset((song_root + "/1080_base_256.jpg").c_str()) != nullptr);
        assert(manager.ResolveAsset(("Resources/" + song_root + "/1080/base_256.jpg").c_str()) != nullptr);
        assert(manager.ResolveAsset(("file:///android_asset/" + song_root + "/base.ogg").c_str()) != nullptr);
        std::string custom_song_id;
        int first_slot = -1;
        bool has_pst = false, has_prs = false, has_ftr = false;
        for (const auto &difficulty : song.at("difficulties")) {
            const int rating_class = difficulty.at("ratingClass").get<int>();
            if (rating_class == 0) has_pst = true;
            if (rating_class == 1) has_prs = true;
            if (rating_class == 2) has_ftr = true;
            if (first_slot < 0 && difficulty.at("rating").get<int>() >= 0) {
                first_slot = rating_class;
            }
        }
        assert(has_pst && has_prs && has_ftr);
        assert(first_slot >= 0);
        assert(manager.IsCustomChartPath(
            ("Resources/" + song_root + "/" + std::to_string(first_slot) + ".aff").c_str(),
            &custom_song_id));
        assert(custom_song_id == song.at("id").get<std::string>());
        assert(!manager.IsCustomChartPath(
            (song_root + "/" + std::to_string(first_slot) + ".aff.bak").c_str()));
        assert(!manager.IsCustomChartPath((song_root + "/base.jpg").c_str()));
        const std::string title = song.at("title_localized").at("en").get<std::string>();
        checked_configured_default |= song.value("artist", "") == "Configured Artist";
        const auto &difficulties = song.at("difficulties");
        if (title == "Beyond the Edge") {
            checked_light_side = true;
            assert(song.at("side").get<int>() == 0);
            assert(song.at("bg").get<std::string>() == "base_light");
            assert(song.at("byd_local_unlock").get<bool>());
            int placeholders = 0;
            bool saw_byd = false;
            for (const auto &difficulty : difficulties) {
                const int rating_class = difficulty.at("ratingClass").get<int>();
                if (rating_class == 3) {
                    saw_byd = true;
                    assert(difficulty.at("rating").get<int>() == 10);
                } else if (rating_class <= 2) {
                    assert(difficulty.at("rating").get<int>() == -1);
                    ++placeholders;
                }
            }
            assert(saw_byd);
            assert(placeholders == 3);
        }
        if (title == "Melodiniq") {
            const auto eternal = std::ranges::find_if(difficulties, [](const auto &difficulty) {
                return difficulty.value("ratingClass", -1) == 4;
            });
            assert(eternal != difficulties.end());
            assert(eternal->at("rating").get<int>() == 11);
            assert(eternal->value("ratingPlus", false));
            assert(std::ranges::find(directory_entries, "4.aff") != directory_entries.end());
            const auto eternal_ids = manager.ListSongIdsForDifficulty(4);
            assert(std::ranges::find(eternal_ids, song.at("id").get<std::string>()) != eternal_ids.end());
            checked_eternal = true;
        }
        if (title == "Bounded Numeric") {
            checked_bounded_numeric = true;
            assert(song.at("bpm_base").get<double>() == 120.0);
            assert(song.at("side").get<int>() == 1);
            assert(song.at("bg").get<std::string>() == "base_conflict");
            assert(song.at("audioPreview").get<int64_t>() == 0);
            assert(song.at("audioPreviewEnd").get<int64_t>() == 30000);
        }
        if (title == "Plain Songlist") {
            checked_plain_songlist = true;
            assert(song.at("artist").get<std::string>() == "Plain Artist");
            assert(song.at("bpm_base").get<double>() == 132.0);
        }
        // Both `songlist` and `songlist.json` ship in this package; `songlist`
        // must win, and the losing spelling must never surface as a song.
        if (title == "Conflict Winner") checked_songlist_conflict = true;
        assert(title != "Conflict Loser");
        if (title == "Bounded Arc") {
            checked_bounded_arc = true;
            assert(song.at("bpm_base").get<double>() == 120.0);
            assert(song.at("side").get<int>() == 0);
            assert(song.at("audioPreview").get<int64_t>() == 0);
            assert(song.at("audioPreviewEnd").get<int64_t>() == 30000);
        }
        for (const auto &difficulty : difficulties) {
            const int rating = difficulty.value("rating", -99);
            const int rating_class = difficulty.at("ratingClass").get<int>();
            const bool plus = difficulty.value("ratingPlus", false);
            if (rating == -1) {
                assert(rating_class <= 2);
                continue;
            }
            assert(rating >= 0);
            if (title == "tiny tales continue" && (rating == 5 || rating == 9)) {
                checked_tiny_tales = true;
                assert(!plus);
            }
            if (title == "Bounded Numeric" && rating_class == 2) {
                assert(rating == 0);
                assert(!plus);
            }
            if (title == "Bounded Arc" && rating_class == 2) {
                assert(rating == 11);
                assert(plus);
                assert(difficulty.at("chartDesigner").get<std::string>() == "Folded Charter");
            }
            found_text_rating_10_plus |= title == "Beyond the Edge" && rating == 10 && plus;
        }
    }
    assert(found_text_rating_10_plus);
    assert(checked_tiny_tales);
    assert(checked_light_side);
    assert(checked_eternal);
    assert(checked_bounded_numeric);
    assert(checked_bounded_arc);
    assert(checked_configured_default);
    assert(checked_plain_songlist);
    assert(checked_songlist_conflict);
    std::string merge_error;
    const std::string merged_json = manager.MergeSonglist(
        R"({"songs":[{"idx":41,"id":"official"}]})", merge_error);
    assert(merge_error.empty());
    const auto merged = nlohmann::json::parse(merged_json, nullptr, false);
    assert(merged.is_object());
    assert(merged.at("songs").size() == expected_songs + 1);
    for (size_t i = 0; i < expected_songs; ++i) {
        assert(merged.at("songs").at(i + 1).at("idx").get<int64_t>() ==
               static_cast<int64_t>(42 + i));
    }
    // Raw ZIP 1.zip packages djmax_wagd.jpg; it must be extracted under the
    // exact 1080 background namespace used by the game.
    assert(!manager.ContainsSongId(""));
    assert(!manager.ContainsSongId("official"));
    assert(!manager.ContainsSongId("ah_not_imported"));
    assert(manager.HasAssetPrefixForTesting("img/bg/1080/ahbg_"));
    assert(manager.HasAssetSuffixForTesting("_clear.png"));
    // Broken/minimal raw packages have no cover and must use the real APK
    // default jackets rather than Default/ImageFile.png.
    assert(manager.HasAssetValueForTesting("@official:img/default_jacket.jpg"));
    assert(manager.HasAssetValueForTesting("@official:img/default_jacket_256.jpg"));
    const std::filesystem::path root(argv[1]);
    assert(std::filesystem::is_regular_file(root / "manifest.json"));
    assert(std::filesystem::is_regular_file(root / "import-report.json"));
    // The duplicate songlist must be reported rather than silently resolved.
    // The handle must be closed before the retry import below: the report
    // writer publishes by rename, which cannot replace an open file on Windows.
    std::string report_text;
    {
        std::ifstream report_file(root / "import-report.json");
        report_text.assign(std::istreambuf_iterator<char>(report_file),
                           std::istreambuf_iterator<char>{});
    }
    const auto report = nlohmann::json::parse(report_text, nullptr, false);
    assert(report.is_object() && report.at("entries").is_array());
    const auto &entries = report.at("entries");
    const auto warning = std::ranges::find_if(entries, [](const auto &entry) {
        return entry.at("package").template get<std::string>() == "songlist-conflict.zip" &&
               entry.at("status").template get<std::string>() == "DEFAULTED_FIELD";
    });
    assert(warning != entries.end());
    assert(warning->at("detail").get<std::string>().find("songlist.json") != std::string::npos);

    // A failed filesystem setup must not latch the manager permanently; a
    // later valid import can retry and publish a complete snapshot.
    const std::filesystem::path blocked_root = root / "blocked-root";
    std::ofstream(blocked_root) << "file";
    auto invalid_settings = settings;
    invalid_settings.root_dir = blocked_root.string();
    invalid_settings.charts_dir = (blocked_root / "charts").string();
    invalid_settings.cache_dir = (blocked_root / "cache").string();
    assert(!manager.ImportForTesting(invalid_settings));
    assert(manager.ImportForTesting(settings));
    std::cout << "importer host test passed songs=" << manager.SongCountForTesting()
              << " assets=" << manager.AssetCountForTesting() << '\n';
    return 0;
}
