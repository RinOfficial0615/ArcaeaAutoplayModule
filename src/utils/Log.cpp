#include "utils/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <ranges>
#include <string_view>
#include <vector>

#include <android/log.h>
#include <magic_enum/magic_enum.hpp>
#include <unistd.h>

#include "config/ModuleConfig.h"

namespace arc_helper {
namespace {

constexpr std::string_view kTruncationMarker = " [TRUNC]";
constexpr auto kLevelNames = [] {
    std::array<const char *, magic_enum::enum_count<LogLevel>()> names{};
    names[magic_enum::enum_index<LogLevel::Debug>()] = "DEBUG";
    names[magic_enum::enum_index<LogLevel::Info>()] = "INFO";
    names[magic_enum::enum_index<LogLevel::Warn>()] = "WARN";
    names[magic_enum::enum_index<LogLevel::Error>()] = "ERROR";
    return names;
}();
constexpr auto kAndroidPriorities = [] {
    std::array<int, magic_enum::enum_count<LogLevel>()> priorities{};
    priorities[magic_enum::enum_index<LogLevel::Debug>()] = ANDROID_LOG_DEBUG;
    priorities[magic_enum::enum_index<LogLevel::Info>()] = ANDROID_LOG_INFO;
    priorities[magic_enum::enum_index<LogLevel::Warn>()] = ANDROID_LOG_WARN;
    priorities[magic_enum::enum_index<LogLevel::Error>()] = ANDROID_LOG_ERROR;
    return priorities;
}();
constexpr size_t kDefaultLevelIndex = magic_enum::enum_index<LogLevel::Info>();

// File naming and rotation policy (README: logs/ keeps five files).
constexpr const char *kLogFileBaseName = "ArcHelper-";
constexpr const char *kLogDirName = "logs";
constexpr const char *kLogFileExtension = ".log";
constexpr size_t kMaxRetainedLogFiles = 5;

size_t LevelIndex(LogLevel level) {
    return magic_enum::enum_index(level).value_or(kDefaultLevelIndex);
}

const char *LevelName(LogLevel level) {
    return kLevelNames[LevelIndex(level)];
}

int AndroidPriority(LogLevel level) {
    return kAndroidPriorities[LevelIndex(level)];
}

void DefaultLogcatWriter(int priority, const char *line) {
    __android_log_print(priority, cfg::module::kLogTag, "%s", line ? line : "");
}

size_t Utf8PrefixLength(std::string_view text, size_t budget) {
    size_t cursor = 0;
    while (cursor < text.size() && cursor < budget) {
        const unsigned char lead = static_cast<unsigned char>(text[cursor]);
        size_t length = 1;
        if ((lead & 0x80u) == 0) length = 1;
        else if ((lead & 0xE0u) == 0xC0u) length = 2;
        else if ((lead & 0xF0u) == 0xE0u) length = 3;
        else if ((lead & 0xF8u) == 0xF0u) length = 4;
        if (cursor + length > text.size() || cursor + length > budget) break;
        bool valid = true;
        for (size_t index = 1; index < length; ++index) {
            if ((static_cast<unsigned char>(text[cursor + index]) & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
        }
        cursor += valid ? length : 1;
    }
    return cursor;
}

std::string LimitLine(std::string line, size_t maximum) {
    if (maximum == 0 || line.size() <= maximum) return line;
    if (maximum <= kTruncationMarker.size()) {
        return std::string(kTruncationMarker.substr(0, maximum));
    }
    line.resize(Utf8PrefixLength(line, maximum - kTruncationMarker.size()));
    line.append(kTruncationMarker);
    return line;
}

std::string FormatMessage(const char *format, va_list args, size_t maximum) {
    if (!format) return {};
    if (maximum != 0) {
        std::string message(maximum + 1, '\0');
        va_list write_args;
        va_copy(write_args, args);
        const int written = std::vsnprintf(message.data(), message.size(), format, write_args);
        va_end(write_args);
        if (written < 0) return std::string("<format error>");
        message.resize(std::min(static_cast<size_t>(written), maximum));
        return message;
    }

    va_list count_args;
    va_copy(count_args, args);
    const int needed = std::vsnprintf(nullptr, 0, format, count_args);
    va_end(count_args);
    if (needed <= 0) return needed == 0 ? std::string{} : std::string("<format error>");

    std::string message(static_cast<size_t>(needed) + 1, '\0');
    va_list write_args;
    va_copy(write_args, args);
    std::vsnprintf(message.data(), message.size(), format, write_args);
    va_end(write_args);
    message.resize(static_cast<size_t>(needed));
    return message;
}

std::tm LocalTime(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

// std::format rather than snprintf: a long __FILE__ path used to be silently cut
// off at the fixed buffer size, and the prefix length is not bounded anyway.
std::string FilePrefix(LogLevel level, const char *source_file, int source_line) {
    const auto now = std::chrono::system_clock::now();
    const std::tm local = LocalTime(std::chrono::system_clock::to_time_t(now));
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    return std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] [{}] [{}:{}] ",
                       local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                       local.tm_min, local.tm_sec, milliseconds.count(), LevelName(level),
                       source_file ? source_file : "?", source_line);
}

std::string LogcatPrefix(const char *source_file, int source_line) {
    return std::format("[{}:{}] ", source_file ? source_file : "?", source_line);
}

} // namespace

Logger &Logger::Instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    std::scoped_lock lock(mutex_);
    CloseFileLocked();
}

void Logger::Configure(const std::string &root_dir, const LoggerConfig &config) {
    std::scoped_lock lock(mutex_);
    CloseFileLocked();
    config_ = config;
    if (config_.file.enabled && !OpenFileLocked(root_dir)) {
        config_.file.enabled = false;
        config_.logcat.enabled = true;
    }
}

void Logger::Log(LogLevel level, const char *file, int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    VLog(level, file, line, format, args);
    va_end(args);
}

void Logger::VLog(LogLevel level, const char *file, int line,
                  const char *format, va_list args) {
    std::scoped_lock lock(mutex_);
    if (LevelIndex(level) < LevelIndex(config_.minimum_level)) return;
    if (!config_.logcat.enabled && (!config_.file.enabled || !file_)) return;

    // Format at the largest configured line budget. A zero file budget means
    // unlimited, which forces a full-length format regardless of logcat.
    size_t formatting_budget = config_.logcat.enabled ? config_.logcat.max_length : 0;
    if (config_.file.enabled && file_) {
        formatting_budget = config_.file.max_length == 0
                                ? 0
                                : std::max(formatting_budget, config_.file.max_length);
    }
    const std::string message = FormatMessage(format, args, formatting_budget);
    if (config_.logcat.enabled) {
        WriteLogcatLocked(level, LimitLine(LogcatPrefix(file, line) + message,
                                           config_.logcat.max_length));
    }
    if (config_.file.enabled && file_) {
        const std::string output = LimitLine(FilePrefix(level, file, line) + message,
                                             config_.file.max_length);
        const bool logcat_was_enabled = config_.logcat.enabled;
        const bool write_ok = std::fwrite(output.data(), 1, output.size(), file_) == output.size() &&
                              std::fputc('\n', file_) != EOF && std::fflush(file_) == 0;
        if (!write_ok) {
            CloseFileLocked();
            config_.file.enabled = false;
            config_.logcat.enabled = true;
            if (!logcat_was_enabled) {
                WriteLogcatLocked(level, LimitLine(LogcatPrefix(file, line) + message,
                                                   config_.logcat.max_length));
            }
            WriteLogcatLocked(LogLevel::Error,
                              LimitLine(LogcatPrefix("Log.cpp", __LINE__) +
                                            "file logging disabled after a write failure",
                                        config_.logcat.max_length));
        }
    }
}

void Logger::CloseFileLocked() {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
    file_path_.clear();
}

bool Logger::OpenFileLocked(const std::string &root_dir) {
    if (root_dir.empty()) return false;
    const std::filesystem::path logs_dir = std::filesystem::path(root_dir) / kLogDirName;
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);
    if (ec) return false;

    const std::tm local = LocalTime(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    const std::string filename =
        std::format("{}{:04}{:02}{:02}-{:02}{:02}{:02}-{}{}", kLogFileBaseName,
                    local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                    local.tm_min, local.tm_sec, static_cast<int>(getpid()), kLogFileExtension);
    const std::filesystem::path path = logs_dir / filename;
    file_ = std::fopen(path.string().c_str(), "ab");
    if (!file_) return false;
    file_path_ = path.string();
    RotateFilesLocked(logs_dir.string());
    return true;
}

void Logger::RotateFilesLocked(const std::string &logs_dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(logs_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        if (name.starts_with(kLogFileBaseName) && it->path().extension() == kLogFileExtension) {
            files.push_back(it->path());
        }
    }
    // Names sort chronologically because the timestamp fields are zero-padded
    // and fixed width, so "oldest first" is just the natural order.
    std::ranges::sort(files, {}, [](const std::filesystem::path &path) {
        return path.filename().string();
    });
    while (files.size() > kMaxRetainedLogFiles) {
        const auto oldest = std::ranges::find_if(files, [this](const auto &path) {
            return path.string() != file_path_;
        });
        if (oldest == files.end()) break;
        std::filesystem::remove(*oldest, ec);
        files.erase(oldest);
    }
}

void Logger::WriteLogcatLocked(LogLevel level, const std::string &line) {
    const LogcatWriter writer = test_logcat_writer_ ? test_logcat_writer_ : DefaultLogcatWriter;
    writer(AndroidPriority(level), line.c_str());
}

#ifdef ARC_HELPER_HOST_TEST
void Logger::SetLogcatWriterForTesting(LogcatWriter writer) {
    std::scoped_lock lock(mutex_);
    test_logcat_writer_ = writer;
}

void Logger::ResetForTesting() {
    std::scoped_lock lock(mutex_);
    CloseFileLocked();
    config_ = {{true, 1024}, {false, 0}};
    test_logcat_writer_ = nullptr;
}
#endif

} // namespace arc_helper
