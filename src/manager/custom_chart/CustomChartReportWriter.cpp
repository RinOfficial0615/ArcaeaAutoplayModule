#include "manager/custom_chart/CustomChartReportWriter.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <set>
#include <span>
#include <utility>

#include <nlohmann/json.hpp>

#include "utils/Log.h"

namespace arc_helper {
namespace {

using Json = nlohmann::json;

struct PendingWrite {
    std::filesystem::path target;
    std::filesystem::path temporary;
    std::filesystem::path backup;
    bool had_original = false;
    bool published = false;
};

std::filesystem::path SiblingScratchPath(const std::filesystem::path &target,
                                         std::string_view purpose) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::format("{}.arc-helper-{}-{}-{}", target.string(), purpose, timestamp,
                       sequence.fetch_add(1, std::memory_order_relaxed));
}

void RemoveScratchFiles(std::span<PendingWrite> writes) {
    for (auto &write : writes) {
        std::error_code ignored;
        std::filesystem::remove(write.temporary, ignored);
        if (!write.had_original) std::filesystem::remove(write.backup, ignored);
    }
}

bool StageWrite(PendingWrite &write, std::string_view data, std::string &error) {
    if (write.target.empty()) {
        error = "empty report path";
        return false;
    }
    std::error_code ec;
    if (!write.target.parent_path().empty()) {
        std::filesystem::create_directories(write.target.parent_path(), ec);
        if (ec) {
            error = "create report directory failed: " + ec.message();
            return false;
        }
    }
    write.temporary = SiblingScratchPath(write.target, "new");
    write.backup = SiblingScratchPath(write.target, "old");
    {
        std::ofstream file(write.temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(data.data(), static_cast<std::streamsize>(data.size()))) {
            std::filesystem::remove(write.temporary, ec);
            error = "write report failed: " + write.target.string();
            return false;
        }
        file.flush();
        if (!file) {
            std::filesystem::remove(write.temporary, ec);
            error = "flush report failed: " + write.target.string();
            return false;
        }
    }
    return true;
}

bool RollbackWrites(std::span<PendingWrite> writes, std::string &error) {
    bool restored = true;
    for (auto &write : writes | std::views::reverse) {
        std::error_code ec;
        if (write.published) {
            std::filesystem::remove(write.target, ec);
            if (ec) restored = false;
        }
        if (write.had_original) {
            ec.clear();
            std::filesystem::rename(write.backup, write.target, ec);
            if (ec) restored = false;
        }
        ec.clear();
        std::filesystem::remove(write.temporary, ec);
    }
    if (!restored) error += "; report rollback incomplete";
    return restored;
}

bool CommitWrites(std::span<PendingWrite> writes, std::string &error) {
    for (auto &write : writes) {
        std::error_code ec;
        write.had_original = std::filesystem::exists(write.target, ec);
        if (ec) {
            error = "inspect report target failed: " + write.target.string() + ": " + ec.message();
            (void)RollbackWrites(writes, error);
            return false;
        }
        if (write.had_original) {
            std::filesystem::rename(write.target, write.backup, ec);
            if (ec) {
                error = "backup report failed: " + write.target.string() + ": " + ec.message();
                write.had_original = false;
                (void)RollbackWrites(writes, error);
                return false;
            }
        }

        ec.clear();
        std::filesystem::rename(write.temporary, write.target, ec);
        if (ec) {
            error = "commit report failed: " + write.target.string() + ": " + ec.message();
            (void)RollbackWrites(writes, error);
            return false;
        }
        write.published = true;
    }

    for (auto &write : writes) {
        if (!write.had_original) continue;
        std::error_code ignored;
        std::filesystem::remove(write.backup, ignored);
    }
    return true;
}

bool CleanupCache(const std::string &cache_dir,
                  const std::vector<std::string> &active_hashes,
                  std::string &error) {
    const std::set<std::string> keep(active_hashes.begin(), active_hashes.end());
    std::error_code ec;
    for (std::filesystem::directory_iterator it(cache_dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code status_ec;
        if (!it->is_directory(status_ec) || status_ec ||
            keep.contains(it->path().filename().string())) {
            continue;
        }
        std::error_code remove_ec;
        std::filesystem::remove_all(it->path(), remove_ec);
        if (remove_ec) {
            error = "remove stale cache failed: " + it->path().string() + ": " +
                    remove_ec.message();
            return false;
        }
    }
    if (ec) {
        error = "scan cache directory failed: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

CustomChartReportWriter::CustomChartReportWriter(std::string root_dir,
                                                 std::string cache_dir)
    : root_dir_(std::move(root_dir)), cache_dir_(std::move(cache_dir)) {}

bool CustomChartReportWriter::Publish(const ImportSnapshot &snapshot,
                                      const std::vector<std::string> &active_hashes,
                                      std::string &error) const {
    error.clear();
    Json packages = Json::array();
    for (const auto &hash : active_hashes) packages.push_back({{"sha256", hash}});
    const Json manifest = {
        {"version", 1},
        {"packages", std::move(packages)},
        {"songs", snapshot.songs.size()},
    };
    Json entries = Json::array();
    for (const auto &diagnostic : snapshot.diagnostics) {
        entries.push_back({
            {"package", diagnostic.package},
            {"item", diagnostic.item},
            {"status", diagnostic.status},
            {"detail", diagnostic.detail},
        });
    }
    const Json report = {
        {"version", 1},
        {"entries", std::move(entries)},
    };

    const std::array<std::string, 2> payloads = {
        manifest.dump(2, ' ', false, Json::error_handler_t::replace) + '\n',
        report.dump(2, ' ', false, Json::error_handler_t::replace) + '\n',
    };
    std::array<PendingWrite, 2> writes = {{
        {.target = std::filesystem::path(root_dir_) / "manifest.json",
         .temporary = {}, .backup = {}, .had_original = false, .published = false},
        {.target = std::filesystem::path(root_dir_) / "import-report.json",
         .temporary = {}, .backup = {}, .had_original = false, .published = false},
    }};
    for (size_t index = 0; index < writes.size(); ++index) {
        if (!StageWrite(writes[index], payloads[index], error)) {
            RemoveScratchFiles(writes);
            return false;
        }
    }
    if (!CommitWrites(writes, error)) return false;

    // Cache removal is garbage collection, not part of snapshot correctness.
    // Running it after the report pair commits guarantees a report failure can
    // never delete assets still referenced by the previous live snapshot.
    std::string cleanup_error;
    if (!CleanupCache(cache_dir_, active_hashes, cleanup_error)) {
        ARC_LOGW("CustomCharts: stale cache cleanup deferred: %s",
                 cleanup_error.c_str());
    }
    return true;
}

} // namespace arc_helper
