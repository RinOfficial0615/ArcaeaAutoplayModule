#include "manager/custom_chart/ArcPackageFormat.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <c4/std/string.hpp>
#include <c4/yml/node.hpp>
#include <c4/yml/parse.hpp>
#include <c4/yml/tree.hpp>

#include "config/CustomChartConfig.h"
#include "utils/BoundedParse.hpp"

namespace arc_helper {
namespace {

using c4::yml::Callbacks;
using c4::yml::ConstNodeRef;
using c4::yml::ErrorDataBasic;
using c4::yml::ErrorDataParse;
using c4::yml::ErrorDataVisit;
using c4::yml::Tree;

struct YamlSession {
    std::jmp_buf jump{};
    std::string error;
};

// The build disables exceptions, so rapidyaml's error callbacks cannot throw.
// longjmp out of the parse instead. This deliberately skips non-trivial
// destructors of frames between setjmp and the callback; only rapidyaml's own
// stack frames live there (no owning RAII objects), and the session tree is
// released by the caller after the jump lands.
[[noreturn]] void YamlFail(YamlSession *session, c4::csubstr msg) {
    if (session) {
        if (msg.str && msg.len) session->error.assign(msg.str, msg.len);
        else session->error = "yaml parse error";
        std::longjmp(session->jump, 1);
    }
    std::abort();
}

[[noreturn]] void OnYamlBasic(c4::csubstr msg, ErrorDataBasic const &, void *user) {
    YamlFail(static_cast<YamlSession *>(user), msg);
}

[[noreturn]] void OnYamlParse(c4::csubstr msg, ErrorDataParse const &, void *user) {
    YamlFail(static_cast<YamlSession *>(user), msg);
}

[[noreturn]] void OnYamlVisit(c4::csubstr msg, ErrorDataVisit const &, void *user) {
    YamlFail(static_cast<YamlSession *>(user), msg);
}

std::string ToString(c4::csubstr value) {
    if (!value.str || !value.len) return {};
    return std::string(value.str, value.len);
}

std::string ChildString(ConstNodeRef node, std::string_view key) {
    if (!node.readable() || !node.is_map()) return {};
    const c4::csubstr name(key.data(), key.size());
    if (!node.has_child(name)) return {};
    const ConstNodeRef child = node.find_child(name);
    if (!child.readable() || !child.has_val()) return {};
    return ToString(child.val());
}

ConstNodeRef ChildNode(ConstNodeRef node, std::string_view key) {
    if (!node.readable() || !node.is_map()) return {};
    const c4::csubstr name(key.data(), key.size());
    if (!node.has_child(name)) return {};
    return node.find_child(name);
}

int ArcSide(std::string_view value) {
    std::string lower = TrimWhitespace(value);
    std::ranges::transform(lower, lower.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    if (lower == "light") return 0;
    if (lower == "conflict") return 1;
    if (lower == "colorless") return 2;
    return -1;
}

bool ParseYamlTree(std::string_view yaml, Tree &tree, std::string &error) {
    error.clear();
    std::string buffer(yaml);
    if (buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF &&
        static_cast<unsigned char>(buffer[1]) == 0xBB &&
        static_cast<unsigned char>(buffer[2]) == 0xBF) {
        buffer.erase(0, 3);
    }
    YamlSession session;
    Callbacks callbacks;
    callbacks.set_user_data(&session);
    callbacks.set_error_basic(&OnYamlBasic);
    callbacks.set_error_parse(&OnYamlParse);
    callbacks.set_error_visit(&OnYamlVisit);
    Tree *owned = nullptr;
    if (setjmp(session.jump) != 0) {
        delete owned;
        error = session.error.empty() ? "yaml parse error" : session.error;
        return false;
    }
    owned = new Tree(callbacks);
    c4::yml::parse_in_arena(c4::to_csubstr(buffer), owned);
    tree = std::move(*owned);
    tree.callbacks(c4::yml::get_callbacks());
    delete owned;
    return true;
}

ConstNodeRef UnwrapRoot(ConstNodeRef root) {
    if (!root.readable()) return root;
    if (root.is_stream() && root.has_children()) {
        return UnwrapRoot(root.child(0));
    }
    if (root.is_doc() && !root.is_seq() && !root.is_map() && root.has_children()) {
        return UnwrapRoot(root.child(0));
    }
    return root;
}

ArcChartSettings DefaultChart(const CustomChartSettings &defaults) {
    ArcChartSettings result;
    result.base_bpm = defaults.default_bpm;
    result.preview_start = defaults.default_preview_start_ms;
    result.preview_end = defaults.default_preview_start_ms + defaults.default_preview_duration_ms;
    return result;
}

void ApplyChartNode(ConstNodeRef node, ArcChartSettings &chart, const CustomChartSettings &defaults) {
    if (!node.readable() || !node.is_map()) return;
    auto set_string = [&](std::string_view key, std::string ArcChartSettings::*field) {
        const std::string value = ChildString(node, key);
        if (!value.empty()) chart.*field = value;
    };
    set_string("chartPath", &ArcChartSettings::chart_path);
    set_string("audioPath", &ArcChartSettings::audio_path);
    set_string("jacketPath", &ArcChartSettings::jacket_path);
    set_string("backgroundPath", &ArcChartSettings::background_path);
    set_string("title", &ArcChartSettings::title);
    set_string("composer", &ArcChartSettings::composer);
    set_string("charter", &ArcChartSettings::charter);
    set_string("illustrator", &ArcChartSettings::illustrator);
    set_string("difficulty", &ArcChartSettings::difficulty);
    set_string("bpmText", &ArcChartSettings::bpm_text);

    ParseBoundedDouble(ChildString(node, "baseBpm"),
                       cfg::custom_charts::kMinimumBpm,
                       cfg::custom_charts::kMaximumBpm,
                       chart.base_bpm);
    ParseBoundedDouble(ChildString(node, "chartConstant"),
                       cfg::custom_charts::kMinimumRating,
                       cfg::custom_charts::kMaximumRating,
                       chart.chart_constant);
    ParseBoundedInt64(ChildString(node, "previewStart"),
                      int64_t{0},
                      cfg::custom_charts::kMaximumPreviewEndMs -
                          defaults.default_preview_duration_ms,
                      chart.preview_start);
    ParseBoundedInt64(ChildString(node, "previewEnd"),
                      int64_t{0},
                      cfg::custom_charts::kMaximumPreviewEndMs,
                      chart.preview_end);

    const ConstNodeRef skin = ChildNode(node, "skin");
    if (skin.readable()) {
        const int side = ArcSide(ChildString(skin, "side"));
        if (side >= cfg::custom_charts::kMinimumSide) chart.side = side;
    }
}

} // namespace

std::vector<ArcIndexItem> ParseArcIndex(std::string_view yaml, std::string &error) {
    Tree tree;
    if (!ParseYamlTree(yaml, tree, error)) return {};
    const ConstNodeRef root = UnwrapRoot(tree.crootref());
    if (!root.readable() || !root.is_seq()) {
        error = "index.yml is not a sequence";
        return {};
    }
    std::vector<ArcIndexItem> items;
    for (ConstNodeRef child : root.children()) {
        if (!child.readable() || !child.is_map()) continue;
        ArcIndexItem item;
        item.directory = ChildString(child, "directory");
        item.identifier = ChildString(child, "identifier");
        item.settings_file = ChildString(child, "settingsFile");
        item.type = ChildString(child, "type");
        int64_t version = 0;
        if (ParseBoundedInt64(ChildString(child, "version"), 0, INT32_MAX, version)) {
            item.version = static_cast<int>(version);
        }
        if (!item.directory.empty()) items.push_back(std::move(item));
    }
    return items;
}

std::vector<ArcChartSettings> ParseArcProject(std::string_view yaml,
                                              const CustomChartSettings &defaults,
                                              std::string &error) {
    Tree tree;
    if (!ParseYamlTree(yaml, tree, error)) return {};
    const ConstNodeRef root = UnwrapRoot(tree.crootref());
    if (!root.readable() || !root.is_map()) {
        error = "project settings are not a mapping";
        return {};
    }
    const ConstNodeRef charts = ChildNode(root, "charts");
    if (!charts.readable() || !charts.is_seq()) {
        error = "charts sequence missing";
        return {};
    }
    std::vector<ArcChartSettings> result;
    for (ConstNodeRef child : charts.children()) {
        ArcChartSettings chart = DefaultChart(defaults);
        ApplyChartNode(child, chart, defaults);
        if (!chart.chart_path.empty()) result.push_back(std::move(chart));
    }
    return result;
}

} // namespace arc_helper
