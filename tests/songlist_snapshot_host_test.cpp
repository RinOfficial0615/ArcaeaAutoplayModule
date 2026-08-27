#include <cassert>
#include <string>

#include <nlohmann/json.hpp>

#include "manager/custom_chart/CustomChartSnapshot.hpp"

namespace {

using arc_helper::ImportedChart;
using arc_helper::ImportedSong;
using arc_helper::ImportSnapshot;

} // namespace

int main() {
    ImportSnapshot snapshot;
    ImportedSong song;
    song.id = "ah_demo_422e6e2f";
    song.title = "Demo Song";
    song.artist = "Artist";
    song.bpm = "180";
    song.bpm_base = 180.0;
    song.bg = "base_light";

    // Slot 3 carries an official-7.0-style Inscribed entry: class 3 plus a
    // display-only alias. The engine keeps runtime slots per ratingClass;
    // the alias must be echoed so the difficulty restyles as Inscribed.
    ImportedChart &inscribed = song.charts[3];
    inscribed.slot = 3;
    inscribed.chart_path = "/cache/demo/3.aff";
    inscribed.source_name = "3.aff";
    inscribed.charter = "Ins Charter";
    inscribed.rating = 12;
    inscribed.rating_plus = true;
    inscribed.rating_class_alias = 1;
    song.has_chart[3] = true;

    ImportedChart &eternal = song.charts[4];
    eternal.slot = 4;
    eternal.chart_path = "/cache/demo/4.aff";
    eternal.rating = 9;
    song.has_chart[4] = true;

    snapshot.songs.push_back(song);

    const auto parsed = nlohmann::json::parse(snapshot.SongsJson(), nullptr, false);
    assert(parsed.is_array() && parsed.size() == 1);
    const auto &entry = parsed.at(0);
    assert(entry.at("byd_local_unlock").get<bool>());
    const auto &difficulties = entry.at("difficulties");
    int aliased = 0;
    bool plain_slot_four = false;
    for (const auto &difficulty : difficulties) {
        const int rating_class = difficulty.at("ratingClass").get<int>();
        if (rating_class == 3) {
            assert(difficulty.value("ratingClassAlias", 0) == 1);
            assert(difficulty.at("rating").get<int>() == 12);
            assert(difficulty.value("ratingPlus", false));
            ++aliased;
        } else {
            assert(difficulty.find("ratingClassAlias") == difficulty.end());
            if (rating_class == 4) plain_slot_four = true;
        }
        // Placeholders stay omitted entirely; emitted entries never carry an
        // alias echo without a real chart behind them.
        assert(rating_class >= 0 && rating_class <= 4);
    }
    assert(aliased == 1);
    assert(entry.at("difficulties").size() ==
           static_cast<size_t>(arc_helper::cfg::custom_charts::kDifficultyCount));

    // Alias echoes ride into the merged official list untouched.
    std::string error;
    const std::string merged =
        snapshot.MergeOfficialSonglist(R"({"songs":[{"idx":7,"id":"official"}]})", error);
    assert(error.empty());
    const auto merged_json = nlohmann::json::parse(merged, nullptr, false);
    assert(merged_json.is_object());
    assert(merged_json.at("songs").at(1).at("difficulties")
               .at(3)
               .value("ratingClassAlias", 0) == 1);
    assert(plain_slot_four);
    return 0;
}
