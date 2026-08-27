#include "manager/custom_chart/CustomChartImporter.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "manager/custom_chart/AffNormalizer.hpp"
#include "manager/custom_chart/ArcPackageFormat.hpp"
#include "manager/custom_chart/CustomChartReportWriter.hpp"
#include "utils/BoundedParse.hpp"
#include "utils/ImageRaster.hpp"
#include "utils/Log.h"
#include "utils/Sha256.hpp"
#include "utils/ZipArchive.hpp"

namespace arc_helper {
namespace {

using zip::Archive;
using zip::Entry;
using Json = nlohmann::json;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(std::string_view value) {
    size_t b = 0, e = value.size();
    while (b < e && std::isspace(static_cast<unsigned char>(value[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1]))) --e;
    std::string out(value.substr(b, e - b));
    if (out.size() >= 2 && ((out.front() == '\'' && out.back() == '\'') ||
                            (out.front() == '"' && out.back() == '"'))) {
        out = out.substr(1, out.size() - 2);
    }
    return out;
}

std::string BaseName(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return std::string(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

std::string Stem(std::string_view path) {
    std::string base = BaseName(path);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    return base;
}

std::string Extension(std::string_view path) {
    const std::string base = BaseName(path);
    const size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? std::string{} : Lower(base.substr(dot));
}

std::string SanitizeId(std::string_view input, std::string_view fallback) {
    std::string out;
    out.reserve(std::min(input.size(), cfg::custom_charts::kMaxSanitizedIdLength));
    for (const unsigned char c : input) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        else if ((c == '_' || c == '-') && !out.empty() && out.back() != '_') out.push_back('_');
        if (out.size() == cfg::custom_charts::kMaxSanitizedIdLength) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string(fallback) : out;
}

std::string MakeSongId(std::string_view source, std::string_view hash,
                       std::string_view fallback, size_t ordinal = 0) {
    std::string suffix = "_" +
                         std::string(hash.substr(0, cfg::custom_charts::kSongIdHashChars));
    if (ordinal) suffix += "_" + std::to_string(ordinal);
    std::string source_id = SanitizeId(source, fallback);
    const size_t prefix_size = cfg::custom_charts::kCustomSongIdPrefix.size();
    const size_t available = cfg::custom_charts::kMaxSongIdLength > prefix_size + suffix.size()
                                 ? cfg::custom_charts::kMaxSongIdLength - prefix_size - suffix.size()
                                 : 0;
    source_id.resize(std::min(source_id.size(), available));
    return std::string(cfg::custom_charts::kCustomSongIdPrefix) + source_id + suffix;
}

bool ReadEntryText(const Archive &archive, const Entry *entry, std::string &out, std::string &error) {
    if (!entry) { error = "entry missing"; return false; }
    if (entry->uncompressed_size > cfg::custom_charts::kMaxTextEntryBytes) {
        error = "text entry size limit";
        return false;
    }
    std::vector<uint8_t> data;
    if (!archive.Extract(*entry, data, error)) return false;
    if (data.size() > cfg::custom_charts::kMaxTextEntryBytes) {
        error = "text entry size limit";
        return false;
    }
    out.assign(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
}

const Entry *FindCaseInsensitive(const Archive &archive, std::string_view path) {
    const std::string wanted = Lower(std::string(path));
    for (const auto &entry : archive.Entries()) {
        if (!entry.directory && Lower(entry.name) == wanted) return &entry;
    }
    return nullptr;
}

// A song may only use files inside its own directory prefix. An empty prefix
// means the archive root, so only root-level entries qualify — never a
// whole-archive scan that could borrow another song's assets.
bool InPrefix(std::string_view name, std::string_view prefix) {
    if (prefix.empty()) return name.find('/') == std::string_view::npos;
    return name.starts_with(prefix);
}

const Entry *FindOneByExtension(const Archive &archive, const std::set<std::string> &extensions,
                                std::string_view prefix = {}) {
    const Entry *found = nullptr;
    for (const auto &entry : archive.Entries()) {
        if (entry.directory || !extensions.contains(Extension(entry.name))) continue;
        if (!InPrefix(entry.name, prefix)) continue;
        if (found) return nullptr;
        found = &entry;
    }
    return found;
}

double FirstAffBpm(std::string_view text, double default_bpm) {
    const size_t timing = text.find("timing(");
    if (timing == std::string_view::npos) return default_bpm;
    const size_t comma1 = text.find(',', timing + 7);
    if (comma1 == std::string_view::npos) return default_bpm;
    const size_t comma2 = text.find(',', comma1 + 1);
    if (comma2 == std::string_view::npos) return default_bpm;
    const std::string token = Trim(text.substr(comma1 + 1, comma2 - comma1 - 1));
    double bpm = default_bpm;
    return ParseBoundedDouble(token,
                              cfg::custom_charts::kMinimumBpm,
                              cfg::custom_charts::kMaximumBpm,
                              bpm)
               ? bpm
               : default_bpm;
}

std::optional<int> SlotFromPathOrDifficulty(std::string_view path, std::string_view difficulty) {
    const std::string stem = Stem(path);
    const int numeric_slot = stem.size() == 1 ? stem[0] - '0' : -1;
    if (numeric_slot >= 0 && numeric_slot < static_cast<int>(cfg::custom_charts::kDifficultyCount)) {
        return numeric_slot;
    }
    const std::string d = Lower(std::string(difficulty));
    if (d.find("past") != std::string::npos) return cfg::custom_charts::kPastDifficulty;
    if (d.find("present") != std::string::npos) return cfg::custom_charts::kPresentDifficulty;
    if (d.find("future") != std::string::npos) return cfg::custom_charts::kFutureDifficulty;
    // 7.0 Inscribed charts share the engine's class-3 slot with Beyond
    // (`ratingClass: 3` + `ratingClassAlias` in official songlists).
    if (d.find("inscribed") != std::string::npos ||
        d.find("beyond") != std::string::npos) {
        return cfg::custom_charts::kBeyondDifficulty;
    }
    if (d.find("eternal") != std::string::npos) return cfg::custom_charts::kEternalDifficulty;
    return std::nullopt;
}

std::string FormatBpm(double bpm) {
    std::ostringstream out;
    if (std::floor(bpm) == bpm) out << static_cast<int64_t>(bpm);
    else out << bpm;
    return out.str();
}

std::string JoinZipPath(std::string_view a, std::string_view b) {
    std::string raw(a);
    if (!raw.empty() && raw.back() != '/') raw.push_back('/');
    raw += b;
    std::string normalized;
    return Archive::NormalizePath(raw, normalized) ? normalized : std::string{};
}

class ZipAffSource final : public aff::Source {
public:
    ZipAffSource(const Archive &archive, std::string_view directory)
        : archive_(&archive), directory_(directory) {}

    std::optional<std::string> ReadRelative(std::string_view from_file,
                                            std::string_view relative) const override {
        std::string from = JoinZipPath(directory_, from_file);
        const size_t slash = from.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string{} : from.substr(0, slash);
        const std::string wanted = JoinZipPath(dir, relative);
        std::string text, error;
        if (wanted.empty() ||
            !ReadEntryText(*archive_, FindCaseInsensitive(*archive_, wanted), text, error)) {
            return std::nullopt;
        }
        return text;
    }

private:
    const Archive *archive_;
    std::string directory_;
};

bool WriteBinaryFile(const std::string &path, std::span<const uint8_t> data, std::string &error) {
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "create asset directory failed: " + ec.message();
            return false;
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size()))) {
        error = "write asset failed: " + path;
        return false;
    }
    return true;
}

bool WriteTextFile(const std::string &path, std::string_view text, std::string &error) {
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "create chart directory failed: " + ec.message();
            return false;
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
        error = "write chart failed: " + path;
        return false;
    }
    return true;
}

const Json *JsonFind(const Json *object, std::string_view key) {
    if (!object || !object->is_object()) return nullptr;
    const auto value = object->find(std::string(key));
    return value == object->end() ? nullptr : &*value;
}

std::string JsonString(const Json *value, std::string fallback = {}) {
    if (value && value->is_string() &&
        value->get_ref<const std::string &>().size() <=
            cfg::custom_charts::kMaxMetadataStringBytes) {
        return value->get_ref<const std::string &>();
    }
    return fallback;
}

double JsonNumber(const Json *value, double fallback) {
    if (value && value->is_number()) {
        const double number = value->get<double>();
        if (std::isfinite(number)) return number;
    }
    return fallback;
}

double JsonBoundedNumber(const Json *value, double fallback, double minimum, double maximum) {
    const double number = JsonNumber(value, fallback);
    return number >= minimum && number <= maximum ? number : fallback;
}

std::string JsonBpmText(const Json *value, double fallback) {
    const std::string text = JsonString(value, {});
    double parsed = 0.0;
    return ParseBoundedDouble(text, cfg::custom_charts::kMinimumBpm,
                              cfg::custom_charts::kMaximumBpm, parsed)
               ? text
               : FormatBpm(fallback);
}

std::optional<int64_t> JsonBoundedInteger(const Json *value, int64_t minimum, int64_t maximum) {
    if (!value || !value->is_number()) return std::nullopt;
    if (value->is_number_unsigned()) {
        const uint64_t number = value->get<uint64_t>();
        if (number > static_cast<uint64_t>(maximum) ||
            number > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return std::nullopt;
        }
        const int64_t result = static_cast<int64_t>(number);
        return result >= minimum && result <= maximum ? std::optional<int64_t>(result)
                                                       : std::nullopt;
    }
    if (value->is_number_integer()) {
        const int64_t result = value->get<int64_t>();
        return result >= minimum && result <= maximum ? std::optional<int64_t>(result)
                                                       : std::nullopt;
    }
    const double number = value->get<double>();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        return std::nullopt;
    }
    return static_cast<int64_t>(number);
}

bool JsonBool(const Json *value, bool fallback) {
    if (value && value->is_boolean()) return value->get<bool>();
    return fallback;
}

const Entry *FindRawAudio(const Archive &archive, std::string_view prefix) {
    for (const char *name : cfg::custom_charts::kAudioFileNames) {
        if (!prefix.empty()) {
            if (const auto *e = FindCaseInsensitive(archive, std::string(prefix) + name)) return e;
            continue;
        }
        if (const auto *e = FindCaseInsensitive(archive, name)) return e;
    }
    return FindOneByExtension(archive, {".ogg", ".wav"}, prefix);
}

const Entry *FindRawJacket(const Archive &archive, std::string_view prefix) {
    for (const char *name : cfg::custom_charts::kJacketFileNames) {
        if (!prefix.empty()) {
            if (const auto *e = FindCaseInsensitive(archive, std::string(prefix) + name)) return e;
            continue;
        }
        if (const auto *e = FindCaseInsensitive(archive, name)) return e;
    }
    const Entry *candidate = nullptr;
    for (const auto &entry : archive.Entries()) {
        if (!InPrefix(entry.name, prefix)) continue;
        const std::string ext = Extension(entry.name);
        const std::string base = Lower(BaseName(entry.name));
        if (entry.directory || (ext != ".jpg" && ext != ".jpeg" && ext != ".png")) continue;
        if (base.find("_256") != std::string::npos) continue;
        if (base.find("base") == std::string::npos) continue;
        if (candidate) return nullptr;
        candidate = &entry;
    }
    return candidate;
}

const Entry *FindRawBackground(const Archive &archive, std::string_view prefix, std::string_view bg) {
    if (bg.empty()) return nullptr;
    const std::string wanted = std::string(bg);
    for (const char *ext : {".jpg", ".jpeg", ".png"}) {
        if (!prefix.empty()) {
            if (const auto *e = FindCaseInsensitive(archive, std::string(prefix) + wanted + ext)) return e;
            continue;
        }
        if (const auto *e = FindCaseInsensitive(archive, wanted + ext)) return e;
    }
    return nullptr;
}

void SetRatingFromConstant(ImportedChart &chart, double constant,
                           const CustomChartSettings &settings) {
    if (!std::isfinite(constant) || constant < cfg::custom_charts::kMinimumChartConstant) return;
    chart.rating = static_cast<int>(std::floor(constant));
    chart.rating_plus = chart.rating >= settings.rating_plus_minimum_rating &&
                        constant - static_cast<double>(chart.rating) >=
                            settings.rating_plus_threshold;
}

void SetRatingFromDifficulty(ImportedChart &chart, std::string_view difficulty,
                             const CustomChartSettings &settings) {
    const std::string value = Trim(difficulty);
    if (value.empty()) return;
    size_t digit = 0;
    while (digit < value.size() && !std::isdigit(static_cast<unsigned char>(value[digit]))) ++digit;
    if (digit == value.size()) return;
    size_t end = digit;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) ++end;
    int64_t rating = 0;
    if (!ParseBoundedInt64(std::string_view(value).substr(digit, end - digit),
                           cfg::custom_charts::kMinimumRating,
                           cfg::custom_charts::kMaximumRating,
                           rating)) {
        return;
    }
    chart.rating = static_cast<int>(rating);
    chart.rating_plus = value.find('+', end) != std::string::npos;
    if (chart.rating < settings.rating_plus_minimum_rating) chart.rating_plus = false;
}

bool FinalizeSongAssets(const Archive &archive,
                        const Entry &audio,
                        const Entry *jacket,
                        const Entry *background,
                        std::string_view cache_base,
                        ImportedSong &song,
                        bool &defaulted_jacket,
                        bool &defaulted_background,
                        std::string &error) {
    defaulted_background = false;
    song.audio_path = std::string(cache_base) + cfg::custom_charts::kExtractedAudioStem +
                      Extension(audio.name);
    if (!archive.ExtractToFile(audio, song.audio_path, error)) return false;

    if (jacket) {
        song.jacket_path = std::string(cache_base) + cfg::custom_charts::kExtractedJacketStem +
                           Extension(jacket->name);
        if (!archive.ExtractToFile(*jacket, song.jacket_path, error)) song.jacket_path.clear();
        song.jacket_256_path = song.jacket_path;
    }
    defaulted_jacket = song.jacket_path.empty();
    if (defaulted_jacket) {
        song.jacket_path = cfg::custom_charts::kDefaultJacketAsset;
        song.jacket_256_path = cfg::custom_charts::kDefaultJacket256Asset;
    }

    if (background) {
        std::vector<uint8_t> raw;
        std::string raster_error;
        if (archive.Extract(*background, raw, error) && !raw.empty()) {
            if (const auto image = NormalizeBackgroundImage(raw, &raster_error)) {
                song.bg = std::string(cfg::custom_charts::kCustomBackgroundPrefix) + song.id;
                song.bg_path = std::string(cache_base) +
                               cfg::custom_charts::kExtractedBackgroundStem + ".jpg";
                if (!WriteBinaryFile(song.bg_path, image->jpeg, error)) {
                    song.bg_path.clear();
                    song.bg = song.side == 0 ? cfg::custom_charts::kLightBackground
                                             : cfg::custom_charts::kConflictBackground;
                    defaulted_background = true;
                }
            } else {
                error.clear();
                song.bg_path.clear();
                song.bg = song.side == 0 ? cfg::custom_charts::kLightBackground
                                         : cfg::custom_charts::kConflictBackground;
                defaulted_background = true;
            }
        } else {
            error.clear();
            song.bg_path.clear();
            song.bg = song.side == 0 ? cfg::custom_charts::kLightBackground
                                     : cfg::custom_charts::kConflictBackground;
            defaulted_background = true;
        }
    } else if (song.bg != cfg::custom_charts::kLightBackground &&
               song.bg != cfg::custom_charts::kConflictBackground) {
        song.bg = song.side == 0 ? cfg::custom_charts::kLightBackground
                                 : cfg::custom_charts::kConflictBackground;
        defaulted_background = true;
    }
    return true;
}

} // namespace

CustomChartImporter::CustomChartImporter(CustomChartSettings settings)
    : settings_(std::move(settings)) {}

void CustomChartImporter::AddDiagnostic(std::string package, std::string item,
                                       std::string status, std::string detail) {
    diagnostics_.push_back({std::move(package), std::move(item), std::move(status), std::move(detail)});
}

void CustomChartImporter::RecordAffDiagnostics(const std::string &package,
                                              const std::string &song_id,
                                              const std::string &source_name,
                                              const aff::Result &normalized) {
    for (const auto &diag : normalized.diagnostics) {
        std::string item = song_id;
        item.push_back(' ');
        item += source_name;
        item.push_back(':');
        item += std::to_string(diag.line);
        item.push_back(' ');
        item += diag.item.empty() ? source_name : diag.item;
        AddDiagnostic(package, std::move(item), diag.status, diag.detail);
        if (diag.status.starts_with("DROPPED")) {
            ARC_LOGW("%s %s:%d %s %s %s",
                     song_id.c_str(), source_name.c_str(), diag.line,
                     diag.item.c_str(), diag.status.c_str(), diag.detail.c_str());
        }
    }
}

std::expected<ImportSnapshot, std::string> CustomChartImporter::Import() {
    if (settings_.root_dir.empty() || settings_.charts_dir.empty() ||
        settings_.cache_dir.empty()) {
        return std::unexpected(std::string("importer paths unavailable"));
    }

    std::vector<std::string> active_hashes;
    std::string error;
    if (!ImportAll(active_hashes, error)) return std::unexpected(std::move(error));

    ImportSnapshot snapshot;
    snapshot.songs = std::move(songs_);
    std::unordered_set<std::string> song_ids;
    song_ids.reserve(snapshot.songs.size());
    for (const auto &song : snapshot.songs) {
        if (!song_ids.insert(song.id).second) {
            return std::unexpected("duplicate custom song id: " + song.id);
        }
        if (!snapshot.assets.RegisterSong(song)) {
            return std::unexpected("custom asset path collision: songs/" + song.id);
        }
    }
    snapshot.diagnostics = std::move(diagnostics_);

    CustomChartReportWriter writer(settings_.root_dir, settings_.cache_dir);
    if (!writer.Publish(snapshot, active_hashes, error)) {
        return std::unexpected(std::move(error));
    }
    return snapshot;
}

bool CustomChartImporter::ImportAll(std::vector<std::string> &active_hashes,
                                    std::string &error) {
    songs_.clear();
    diagnostics_.clear();
    active_hashes.clear();
    std::error_code ec;
    std::filesystem::create_directories(settings_.charts_dir, ec);
    if (ec) {
        error = "create charts directory failed: " + ec.message();
        return false;
    }
    ec.clear();
    std::filesystem::create_directories(settings_.cache_dir, ec);
    if (ec) {
        error = "create cache directory failed: " + ec.message();
        return false;
    }
    std::vector<std::filesystem::path> packages;
    for (std::filesystem::directory_iterator it(settings_.charts_dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code status_ec;
        if (!it->is_regular_file(status_ec) || status_ec) continue;
        const std::string ext = Lower(it->path().extension().string());
        if (ext == ".arcpkg" || ext == ".zip") packages.push_back(it->path());
    }
    if (ec) {
        error = "scan charts directory failed: " + ec.message();
        return false;
    }
    std::sort(packages.begin(), packages.end());
    size_t imported_song_count = 0;
    std::unordered_set<std::string> seen_hashes;
    seen_hashes.reserve(packages.size());
    for (const auto &path : packages) {
        std::string hash_error;
        const std::string hash = crypto::Sha256FileHex(path.string(), &hash_error);
        if (hash.empty()) {
            AddDiagnostic(path.filename().string(), "package", "SKIPPED_SONG", "sha256: " + hash_error);
            continue;
        }
        if (!seen_hashes.insert(hash).second) {
            AddDiagnostic(path.filename().string(), "package", "SKIPPED_PACKAGE",
                          "duplicate package content");
            continue;
        }
        active_hashes.push_back(hash);
        const size_t songs_before = songs_.size();
        if (!ImportPackage(path.string(), hash)) {
            AddDiagnostic(path.filename().string(), "package", "SKIPPED_PACKAGE",
                          "package could not be imported");
        }
        imported_song_count += songs_.size() - songs_before;
    }
    if (!packages.empty() && imported_song_count == 0) {
        error = "no importable chart packages";
        return false;
    }
    return true;
}

void ApplySettingsOverrides(ImportedSong &song, const CustomChartSettings &settings) {
    if (settings.override_side) song.side = *settings.override_side;
    if (settings.override_background) {
        song.bg = *settings.override_background;
        song.bg_path.clear();
    }
}

ImportedSong CustomChartImporter::MakeDefaultSong() const {
    ImportedSong song;
    song.artist = settings_.default_artist;
    song.bpm = FormatBpm(settings_.default_bpm);
    song.bpm_base = settings_.default_bpm;
    song.side = settings_.default_side;
    song.bg = settings_.default_background;
    song.preview_start = settings_.default_preview_start_ms;
    song.preview_end = settings_.default_preview_start_ms +
                       settings_.default_preview_duration_ms;
    for (auto &chart : song.charts) {
        chart.charter = settings_.default_designer;
        chart.jacket_designer = settings_.default_designer;
        chart.rating = settings_.default_rating;
    }
    return song;
}

bool CustomChartImporter::ImportPackage(const std::string &path, const std::string &hash) {
    const std::string ext = Lower(std::filesystem::path(path).extension().string());
    return ext == ".arcpkg" ? ImportArcPackage(path, hash) : ImportRawZip(path, hash);
}

bool CustomChartImporter::ImportArcPackage(const std::string &path, const std::string &hash) {
    Archive archive;
    std::string error;
    const std::string package_name = std::filesystem::path(path).filename().string();
    if (!archive.Open(path, error)) {
        AddDiagnostic(package_name, "package", "SKIPPED_SONG", error);
        return false;
    }
    std::string index_text;
    if (!ReadEntryText(archive, FindCaseInsensitive(archive, "index.yml"), index_text, error)) {
        AddDiagnostic(package_name, "index.yml", "SKIPPED_SONG", error);
        return false;
    }
    std::string yaml_error;
    const auto index = ParseArcIndex(index_text, yaml_error);
    if (index.empty()) {
        AddDiagnostic(package_name, "index.yml", "SKIPPED_SONG",
                      yaml_error.empty() ? "no level entries" : yaml_error);
        return false;
    }
    size_t ordinal = 0;
    for (const auto &item : index) {
        ++ordinal;
        if (!item.type.empty() && item.type != "level") {
            AddDiagnostic(package_name, item.identifier.empty() ? item.directory : item.identifier,
                          "SKIPPED_SONG", "unsupported type: " + item.type);
            continue;
        }
        const std::string settings_path = JoinZipPath(item.directory, item.settings_file);
        std::string project_text;
        if (settings_path.empty() || !ReadEntryText(archive, FindCaseInsensitive(archive, settings_path), project_text, error)) {
            AddDiagnostic(package_name, item.identifier, "SKIPPED_SONG", "project: " + error);
            continue;
        }
        const auto settings = ParseArcProject(project_text, settings_, yaml_error);
        if (settings.empty()) {
            AddDiagnostic(package_name, item.identifier, "SKIPPED_SONG",
                          "project: " + (yaml_error.empty() ? std::string("no charts") : yaml_error));
            continue;
        }
        ImportedSong song = MakeDefaultSong();
        song.source_id = item.identifier.empty() ? item.directory : item.identifier;
        song.id = MakeSongId(song.source_id, hash, settings_.fallback_song_id,
                             ordinal > 1 ? ordinal : 0);
        const std::string cache_base = settings_.cache_dir + "/" + hash + "/" + song.id;
        const Entry *audio_entry = nullptr;
        const Entry *jacket_entry = nullptr;
        const Entry *background_entry = nullptr;
        for (const auto &chart : settings) {
            const auto slot = SlotFromPathOrDifficulty(chart.chart_path, chart.difficulty);
            if (!slot || *slot < 0 ||
                *slot >= static_cast<int64_t>(cfg::custom_charts::kDifficultyCount)) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART", "unmapped difficulty slot");
                continue;
            }
            if (song.has_chart[static_cast<size_t>(*slot)]) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART",
                              "duplicate difficulty slot");
                continue;
            }
            const std::string chart_zip = JoinZipPath(item.directory, chart.chart_path);
            const Entry *chart_entry = FindCaseInsensitive(archive, chart_zip);
            if (!chart_entry) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART", "AFF missing");
                continue;
            }
            if (!audio_entry) audio_entry = FindCaseInsensitive(archive, JoinZipPath(item.directory, chart.audio_path));
            if (!jacket_entry && !chart.jacket_path.empty())
                jacket_entry = FindCaseInsensitive(archive, JoinZipPath(item.directory, chart.jacket_path));
            if (!background_entry && !chart.background_path.empty()) {
                background_entry = FindCaseInsensitive(archive, JoinZipPath(item.directory, chart.background_path));
            }
            if (!audio_entry) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART", "audio missing");
                continue;
            }
            const std::string chart_out = cache_base + "/" + std::to_string(*slot) + ".aff";
            std::string aff_text;
            if (!ReadEntryText(archive, chart_entry, aff_text, error)) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART", error);
                continue;
            }
            ZipAffSource aff_files(archive, item.directory);
            const auto normalized = aff::Normalize(aff_text, chart.chart_path, &aff_files);
            RecordAffDiagnostics(package_name, song.id, chart.chart_path, normalized);
            if (!WriteTextFile(chart_out, normalized.text, error)) {
                AddDiagnostic(package_name, chart.chart_path, "SKIPPED_CHART", error);
                continue;
            }
            auto &out = song.charts[*slot];
            out.slot = *slot; out.chart_path = chart_out; out.source_name = chart.chart_path;
            out.charter = chart.charter.empty() ? settings_.default_designer : chart.charter;
            out.jacket_designer = chart.illustrator.empty()
                                      ? settings_.default_designer
                                      : chart.illustrator;
            if (chart.chart_constant >= cfg::custom_charts::kMinimumChartConstant) {
                SetRatingFromConstant(out, chart.chart_constant, settings_);
            }
            else SetRatingFromDifficulty(out, chart.difficulty, settings_);
            song.has_chart[*slot] = true;
            if (song.title.empty()) song.title = chart.title;
            if (song.artist == settings_.default_artist && !chart.composer.empty()) {
                song.artist = chart.composer;
            }
            if (chart.base_bpm >= cfg::custom_charts::kMinimumBpm) {
                song.bpm_base = chart.base_bpm;
            }
            if (!chart.bpm_text.empty()) song.bpm = chart.bpm_text;
            if (chart.side >= cfg::custom_charts::kMinimumSide) {
                song.side = chart.side;
                if (song.bg == cfg::custom_charts::kConflictBackground && chart.side == 0) {
                    song.bg = cfg::custom_charts::kLightBackground;
                }
            }
            song.preview_start = chart.preview_start;
            song.preview_end = chart.preview_end > chart.preview_start
                                   ? chart.preview_end
                                   : chart.preview_start + settings_.default_preview_duration_ms;
        }
        if (std::none_of(song.has_chart.begin(), song.has_chart.end(), [](bool v) { return v; }) || !audio_entry) {
            AddDiagnostic(package_name, song.source_id, "SKIPPED_SONG", "no playable chart");
            continue;
        }
        song.title = song.title.empty() ? item.directory : song.title;
        if (song.bpm.empty()) song.bpm = FormatBpm(song.bpm_base);
        bool defaulted_jacket = false;
        bool defaulted_background = false;
        if (!FinalizeSongAssets(archive, *audio_entry, jacket_entry, background_entry,
                                cache_base, song, defaulted_jacket, defaulted_background, error)) {
            AddDiagnostic(package_name, song.source_id, "SKIPPED_SONG", "audio: " + error);
            continue;
        }
        if (defaulted_jacket) {
            AddDiagnostic(package_name, song.source_id, "DEFAULTED_FIELD", "jacket");
        }
        if (defaulted_background) {
            AddDiagnostic(package_name, song.source_id, "DEFAULTED_FIELD", "background");
        }
        if (Extension(audio_entry->name) != ".ogg") {
            AddDiagnostic(package_name, song.source_id, "NON_OGG_AUDIO",
                          "manual convert to OGG Vorbis");
        }
        ApplySettingsOverrides(song, settings_);
        songs_.push_back(std::move(song));
        AddDiagnostic(package_name, songs_.back().id, "LOADED", "arcpkg");
    }
    return true;
}

bool CustomChartImporter::ImportRawZip(const std::string &path, const std::string &hash) {
    Archive archive;
    std::string error;
    const std::string package_name = std::filesystem::path(path).filename().string();
    if (!archive.Open(path, error)) {
        AddDiagnostic(package_name, "package", "SKIPPED_SONG", error);
        return false;
    }
    Json metadata;
    bool metadata_ok = false;
    if (const Entry *songlist = FindCaseInsensitive(archive, "songlist.json")) {
        std::string text;
        if (ReadEntryText(archive, songlist, text, error)) {
            metadata = Json::parse(text, nullptr, false);
            const Json *songs = JsonFind(&metadata, "songs");
            metadata_ok = !metadata.is_discarded() && songs && songs->is_array();
        }
        if (!metadata_ok) {
            const std::string detail = error.empty()
                                           ? "malformed; fallback discovery"
                                           : error + "; fallback discovery";
            AddDiagnostic(package_name, "songlist.json", "DEFAULTED_FIELD", detail);
        }
    }

    struct RawCandidate {
        const Json *metadata = nullptr;
        std::string prefix;
        std::string fallback_id;
    };
    std::vector<RawCandidate> candidates;
    if (metadata_ok) {
        size_t metadata_index = 0;
        for (const auto &value : *JsonFind(&metadata, "songs")) {
            ++metadata_index;
            if (value.is_object()) {
                candidates.push_back({&value, {}, Stem(package_name)});
            } else {
                AddDiagnostic(package_name, "songs[" + std::to_string(metadata_index - 1) + "]",
                              "SKIPPED_SONG", "metadata entry is not an object");
            }
        }
    } else {
        // Metadata-free fallback groups AFF files by their containing directory.
        // This keeps damage isolated when a ZIP contains several independent
        // chart folders instead of collapsing the entire package into one item.
        std::set<std::string> prefixes;
        for (const auto &entry : archive.Entries()) {
            if (entry.directory || Extension(entry.name) != ".aff") continue;
            const size_t slash = entry.name.find_last_of('/');
            prefixes.insert(slash == std::string::npos ? std::string{} : entry.name.substr(0, slash + 1));
        }
        for (const auto &prefix : prefixes) {
            if (!FindRawAudio(archive, prefix)) continue;
            std::string fallback_id = Stem(package_name);
            if (!prefix.empty()) {
                const std::string without_slash = prefix.substr(0, prefix.size() - 1);
                fallback_id = BaseName(without_slash);
            }
            candidates.push_back({nullptr, prefix, std::move(fallback_id)});
        }
    }
    if (candidates.empty()) candidates.push_back({nullptr, {}, Stem(package_name)});

    size_t ordinal = 0;
    for (const auto &candidate : candidates) {
        const Json *value = candidate.metadata;
        ++ordinal;
        ImportedSong song = MakeDefaultSong();
        song.source_id = value ? JsonString(JsonFind(value, "id"), candidate.fallback_id) : candidate.fallback_id;
        song.id = MakeSongId(song.source_id, hash, settings_.fallback_song_id,
                             ordinal > 1 ? ordinal : 0);
        std::string prefix = candidate.prefix;
        if (value) {
            const std::string metadata_prefix = song.source_id + "/";
            if (FindRawAudio(archive, metadata_prefix)) prefix = metadata_prefix;
        }
        const Entry *audio = FindRawAudio(archive, prefix);
        if (!audio) {
            AddDiagnostic(package_name, song.source_id, "SKIPPED_SONG", "audio missing or ambiguous");
            continue;
        }
        song.title = Stem(package_name);
        if (value) {
            if (const auto *localized = JsonFind(value, "title_localized")) {
                if (const auto *en = JsonFind(localized, "en")) song.title = JsonString(en, song.title);
                else if (localized->is_object() && !localized->empty())
                    song.title = JsonString(&localized->begin().value(), song.title);
            }
            song.artist = JsonString(JsonFind(value, "artist"), settings_.default_artist);
            song.bpm_base = JsonBoundedNumber(JsonFind(value, "bpm_base"),
                                               settings_.default_bpm,
                                               cfg::custom_charts::kMinimumBpm,
                                               cfg::custom_charts::kMaximumBpm);
            song.bpm = JsonBpmText(JsonFind(value, "bpm"), song.bpm_base);
            song.side = static_cast<int>(JsonBoundedInteger(
                                              JsonFind(value, "side"),
                                              cfg::custom_charts::kMinimumSide,
                                              cfg::custom_charts::kMaximumSide)
                                              .value_or(settings_.default_side));
            song.bg = JsonString(JsonFind(value, "bg"), settings_.default_background);
            song.preview_start = JsonBoundedInteger(
                                     JsonFind(value, "audioPreview"),
                                     int64_t{0},
                                     cfg::custom_charts::kMaximumPreviewEndMs -
                                         settings_.default_preview_duration_ms)
                                     .value_or(settings_.default_preview_start_ms);
            song.preview_end = JsonBoundedInteger(
                                   JsonFind(value, "audioPreviewEnd"),
                                   int64_t{0},
                                   cfg::custom_charts::kMaximumPreviewEndMs)
                                   .value_or(song.preview_start +
                                             settings_.default_preview_duration_ms);
            if (song.preview_end <= song.preview_start) {
                song.preview_end = song.preview_start + settings_.default_preview_duration_ms;
            }
        }
        const std::string cache_base = settings_.cache_dir + "/" + hash + "/" + song.id;
        const Entry *background = FindRawBackground(archive, prefix, song.bg);

        std::map<int, const Json *> difficulty_meta;
        if (value) if (const auto *difficulties = JsonFind(value, "difficulties"); difficulties && difficulties->is_array()) {
            for (const auto &difficulty : *difficulties) {
                const auto slot = JsonBoundedInteger(
                    JsonFind(&difficulty, "ratingClass"),
                    cfg::custom_charts::kMinimumRating,
                    static_cast<int64_t>(cfg::custom_charts::kDifficultyCount - 1));
                if (slot) {
                    const int slot_value = static_cast<int>(*slot);
                    if (!difficulty_meta.emplace(slot_value, &difficulty).second) {
                        AddDiagnostic(package_name,
                                      song.source_id + "/" + std::to_string(slot_value),
                                      "SKIPPED_CHART",
                                      "duplicate difficulty slot");
                    }
                }
            }
        }
        std::vector<std::pair<int, const Entry *>> charts;
        for (size_t slot = 0; slot < cfg::custom_charts::kDifficultyCount; ++slot) {
            const Entry *entry = nullptr;
            if (!prefix.empty()) entry = FindCaseInsensitive(archive, prefix + std::to_string(slot) + ".aff");
            if (!entry && prefix.empty()) {
                entry = FindCaseInsensitive(archive, std::to_string(slot) + ".aff");
            }
            if (entry) charts.emplace_back(slot, entry);
        }
        if (charts.empty()) {
            const Entry *only = FindOneByExtension(archive, {".aff"}, prefix);
            if (only) {
                charts.emplace_back(
                    settings_.default_chart_difficulty, only);
            }
        }
        for (const auto &[slot, entry] : charts) {
            const std::string chart_out = cache_base + "/" + std::to_string(slot) + ".aff";
            std::string aff_text;
            if (!ReadEntryText(archive, entry, aff_text, error)) {
                AddDiagnostic(package_name, entry->name, "SKIPPED_CHART", error);
                continue;
            }
            ZipAffSource aff_files(archive, prefix);
            const auto normalized = aff::Normalize(aff_text, entry->name, &aff_files);
            RecordAffDiagnostics(package_name, song.id, entry->name, normalized);
            if (!WriteTextFile(chart_out, normalized.text, error)) {
                AddDiagnostic(package_name, entry->name, "SKIPPED_CHART", error);
                continue;
            }
            auto &chart = song.charts[slot];
            chart.slot = slot; chart.chart_path = chart_out; chart.source_name = entry->name;
            if (const auto it = difficulty_meta.find(slot); it != difficulty_meta.end()) {
                chart.charter = JsonString(JsonFind(it->second, "chartDesigner"),
                                           settings_.default_designer);
                chart.jacket_designer = JsonString(JsonFind(it->second, "jacketDesigner"),
                                                   settings_.default_designer);
                chart.rating = static_cast<int>(
                    JsonBoundedInteger(JsonFind(it->second, "rating"),
                                       cfg::custom_charts::kMinimumRating,
                                       cfg::custom_charts::kMaximumRating)
                        .value_or(settings_.default_rating));
                chart.rating_plus = JsonBool(JsonFind(it->second, "ratingPlus"), false);
                chart.rating_class_alias = static_cast<int>(
                    JsonBoundedInteger(JsonFind(it->second, "ratingClassAlias"),
                                       cfg::custom_charts::kMinimumRatingClassAlias,
                                       cfg::custom_charts::kMaximumRatingClassAlias)
                        .value_or(0));
            }
            song.has_chart[slot] = true;
            if (!metadata_ok) {
                std::string aff;
                if (ReadEntryText(archive, entry, aff, error)) {
                    song.bpm_base = FirstAffBpm(aff, settings_.default_bpm);
                    song.bpm = FormatBpm(song.bpm_base);
                }
            }
        }
        if (std::none_of(song.has_chart.begin(), song.has_chart.end(), [](bool v) { return v; })) {
            AddDiagnostic(package_name, song.source_id, "SKIPPED_SONG", "chart missing or ambiguous");
            continue;
        }
        const Entry *jacket = FindRawJacket(archive, prefix);
        bool defaulted_jacket = false;
        bool defaulted_background = false;
        if (!FinalizeSongAssets(archive, *audio, jacket, background,
                                cache_base, song, defaulted_jacket, defaulted_background, error)) {
            AddDiagnostic(package_name, song.source_id, "SKIPPED_SONG", "audio: " + error);
            continue;
        }
        if (defaulted_jacket) {
            AddDiagnostic(package_name, song.source_id, "DEFAULTED_FIELD", "jacket");
        }
        if (defaulted_background) {
            AddDiagnostic(package_name, song.source_id, "DEFAULTED_FIELD", "background");
        }
        if (Extension(audio->name) != ".ogg") {
            AddDiagnostic(package_name, song.source_id, "NON_OGG_AUDIO",
                          "manual convert to OGG Vorbis");
        }
        if (!value) {
            AddDiagnostic(package_name,
                          song.source_id,
                          "DEFAULTED_FIELD",
                          "metadata side=1 bg=base_conflict");
        }
        ApplySettingsOverrides(song, settings_);
        songs_.push_back(std::move(song));
        AddDiagnostic(package_name, songs_.back().id, "LOADED", metadata_ok ? "raw_zip" : "raw_zip_fallback");
    }
    return true;
}

} // namespace arc_helper
