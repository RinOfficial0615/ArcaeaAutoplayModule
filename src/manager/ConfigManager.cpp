#include "manager/ConfigManager.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "utils/Log.h"

namespace arc_helper {
namespace {

constexpr size_t kMaxConfigBytes = 64 * 1024;

bool EnsureDirectory(const std::string &path, const char *label) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        ARC_LOGE("Failed to create %s %s: %s", label, path.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool AtomicReplace(const std::filesystem::path &source,
                   const std::filesystem::path &destination) {
#ifdef _WIN32
    return MoveFileExA(source.string().c_str(), destination.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    // Flush the temp file to disk before the rename so a crash cannot leave
    // the config truncated behind an already-swapped directory entry.
    const int fd = open(source.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)fsync(fd);
        (void)close(fd);
    }
    return std::rename(source.c_str(), destination.c_str()) == 0;
#endif
}

} // namespace

ConfigManager &ConfigManager::Instance() {
    static ConfigManager manager;
    return manager;
}

void ConfigManager::SetRootDirLocked(const std::string &root_dir) {
    const std::filesystem::path root_path(root_dir);
    root_dir_ = root_path.string();
    charts_dir_ = root_dir_.empty() ? std::string{} : (root_path / "charts").string();
    cache_dir_ = root_dir_.empty() ? std::string{} : (root_path / "cache").string();
    logs_dir_ = root_dir_.empty() ? std::string{} : (root_path / "logs").string();
    data_ = nlohmann::json::object();
    loaded_ = false;
}

void ConfigManager::SetPackageName(const char *package_name) {
    if (!package_name || package_name[0] == '\0') return;
    std::scoped_lock lock(mutex_);
    if (package_name_ == package_name && !root_dir_.empty()) return;
    package_name_ = package_name;
    SetRootDirLocked("/sdcard/Android/data/" + package_name_ + "/files/ArcHelper");
}

void ConfigManager::SetRootDir(const std::string &root_dir) {
    if (root_dir.empty()) return;
    std::scoped_lock lock(mutex_);
    if (root_dir_ == root_dir) return;
    SetRootDirLocked(root_dir);
}

bool ConfigManager::RootAvailable() const {
    std::scoped_lock lock(mutex_);
    return loaded_ && !root_dir_.empty();
}

bool ConfigManager::Load() {
    std::scoped_lock lock(mutex_);
    if (loaded_) return !root_dir_.empty();
    data_ = nlohmann::json::object();
    if (root_dir_.empty()) {
        ARC_LOGE("ArcHelper root unavailable; using in-memory defaults");
        loaded_ = true;
        return false;
    }

    if (!EnsureDirectory(root_dir_, "root")) return false;
    if (!EnsureDirectory(charts_dir_, "charts directory")) return false;
    if (!EnsureDirectory(cache_dir_, "cache directory")) return false;

    const std::filesystem::path path = std::filesystem::path(root_dir_) / "config.json";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        loaded_ = true;
        return true;
    }

    std::string text(kMaxConfigBytes + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read < 0 || static_cast<size_t>(bytes_read) > kMaxConfigBytes) {
        ARC_LOGE("oversized %s; regenerating defaults", path.string().c_str());
        loaded_ = true;
        return true;
    }
    if (input.bad()) {
        ARC_LOGE("failed to read %s; regenerating defaults", path.string().c_str());
        loaded_ = true;
        return true;
    }
    text.resize(static_cast<size_t>(bytes_read));

    auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        ARC_LOGE("malformed %s; regenerating defaults", path.string().c_str());
        loaded_ = true;
        return true;
    }
    data_ = std::move(parsed);
    loaded_ = true;
    return true;
}

bool ConfigManager::Save() {
    std::scoped_lock lock(mutex_);
    if (root_dir_.empty()) return false;

    if (!EnsureDirectory(root_dir_, "root")) return false;

    const std::filesystem::path path = std::filesystem::path(root_dir_) / "config.json";
    const std::filesystem::path temporary = path.string() + ".tmp";
    const auto cleanup = [&] {
        std::error_code cleanup_ec;
        std::filesystem::remove(temporary, cleanup_ec);
    };
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            ARC_LOGE("Failed to open %s", temporary.string().c_str());
            cleanup();
            return false;
        }
        output << data_.dump(2) << '\n';
        output.flush();
        if (!output) {
            ARC_LOGE("Failed to write %s", temporary.string().c_str());
            cleanup();
            return false;
        }
    }

    if (AtomicReplace(temporary, path)) return true;
    cleanup();
    ARC_LOGE("Failed to replace %s", path.string().c_str());
    return false;
}

// nlohmann's object keys are std::string but its lookup overloads accept
// anything usable as a key (its comparator is a transparent std::less<>), so
// string_view keys reach the map without building a temporary std::string.
nlohmann::json &ConfigManager::GetObjectLocked(std::string_view section,
                                                std::string_view subsection) {
    nlohmann::json &section_object = data_[section];
    if (!section_object.is_object()) section_object = nlohmann::json::object();
    if (subsection.empty()) return section_object;

    nlohmann::json &subsection_object = section_object[subsection];
    if (!subsection_object.is_object()) subsection_object = nlohmann::json::object();
    return subsection_object;
}

const nlohmann::json *ConfigManager::FindObjectLocked(std::string_view section,
                                                      std::string_view subsection) const {
    if (!data_.contains(section)) return nullptr;
    const nlohmann::json &section_object = data_.at(section);
    if (!section_object.is_object()) return nullptr;
    if (subsection.empty()) return &section_object;

    if (!section_object.contains(subsection)) return nullptr;
    const nlohmann::json &subsection_object = section_object.at(subsection);
    return subsection_object.is_object() ? &subsection_object : nullptr;
}

void ConfigManager::EnsureObject(std::string_view section, std::string_view subsection) {
    std::scoped_lock lock(mutex_);
    GetObjectLocked(section, subsection);
}

#ifdef ARC_HELPER_HOST_TEST
void ConfigManager::SetRootDirForTesting(const std::string &root_dir) {
    std::scoped_lock lock(mutex_);
    package_name_ = "host.test";
    SetRootDirLocked(root_dir);
}

void ConfigManager::ResetForTesting(const std::string &root_dir) {
    std::scoped_lock lock(mutex_);
    package_name_ = root_dir.empty() ? std::string{} : "host.test";
    SetRootDirLocked(root_dir);
}
#endif

} // namespace arc_helper
