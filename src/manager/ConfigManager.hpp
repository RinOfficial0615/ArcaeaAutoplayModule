#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace arc_helper {

namespace config_detail {

template <typename T>
struct StorageType {
    using Decayed = std::decay_t<T>;
    using type = std::conditional_t<
        std::same_as<Decayed, const char *> || std::same_as<Decayed, char *>,
        std::string,
        Decayed>;
};

template <typename T>
using StorageTypeT = typename StorageType<T>::type;

template <typename T>
concept ConfigScalar =
    std::same_as<std::remove_cvref_t<T>, bool> ||
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>> ||
    std::same_as<std::remove_cvref_t<T>, std::string>;

template <typename Validator, typename T>
concept ConfigValidator =
    ConfigScalar<T> && std::predicate<Validator, const T &>;

inline std::string NormalizeDefault(const char *value) {
    return value ? std::string(value) : std::string{};
}

template <typename T>
StorageTypeT<T> NormalizeDefault(T &&value) {
    return StorageTypeT<T>(std::forward<T>(value));
}

template <ConfigScalar T>
bool ReadJsonValue(const nlohmann::json &value, T &result) {
    if constexpr (std::same_as<T, bool>) {
        if (!value.is_boolean()) return false;
        result = value.get<bool>();
        return true;
    } else if constexpr (std::same_as<T, std::string>) {
        if (!value.is_string()) return false;
        result = value.get_ref<const std::string &>();
        return true;
    } else if constexpr (std::integral<T>) {
        if (value.is_number_unsigned()) {
            const uint64_t raw = value.get<uint64_t>();
            if (!std::in_range<T>(raw)) return false;
            result = static_cast<T>(raw);
            return true;
        }
        if (value.is_number_integer()) {
            const int64_t raw = value.get<int64_t>();
            if (!std::in_range<T>(raw)) return false;
            result = static_cast<T>(raw);
            return true;
        }
        return false;
    } else if constexpr (std::floating_point<T>) {
        if (!value.is_number()) return false;
        const double raw = value.get<double>();
        if (!std::isfinite(raw) || raw < std::numeric_limits<T>::lowest() ||
            raw > std::numeric_limits<T>::max()) {
            return false;
        }
        result = static_cast<T>(raw);
        return true;
    }
    return false;
}

} // namespace config_detail

class ConfigManager {
public:
    static ConfigManager &Instance();

    void SetPackageName(const char *package_name);
    void SetRootDir(const std::string &root_dir);
    bool Load();
    bool Save();

    bool RootAvailable() const;
    std::string PackageName() const {
        std::scoped_lock lock(mutex_);
        return package_name_;
    }
    std::string RootDir() const {
        std::scoped_lock lock(mutex_);
        return root_dir_;
    }
    std::string ChartsDir() const {
        std::scoped_lock lock(mutex_);
        return charts_dir_;
    }
    std::string CacheDir() const {
        std::scoped_lock lock(mutex_);
        return cache_dir_;
    }
    std::string LogsDir() const {
        std::scoped_lock lock(mutex_);
        return logs_dir_;
    }

    template <config_detail::ConfigScalar T>
    T Read(std::string_view section, std::string_view key, T default_value) {
        return ReadValidated(section, {}, key, std::move(default_value),
                             [](const T &) { return true; });
    }

    template <config_detail::ConfigScalar T, typename Validator>
        requires config_detail::ConfigValidator<Validator, T>
    T Read(std::string_view section, std::string_view key, T default_value,
           Validator validator) {
        return ReadValidated(section, {}, key, std::move(default_value),
                             std::move(validator));
    }

    template <config_detail::ConfigScalar T>
    T Read(std::string_view section, std::string_view key, T default_value,
           T minimum, T maximum) {
        return ReadValidated(
            section, {}, key, std::move(default_value),
            [minimum, maximum](const T &value) { return value >= minimum && value <= maximum; });
    }

    template <config_detail::ConfigScalar T>
    T Read(std::string_view section, std::string_view subsection,
           std::string_view key, T default_value) {
        return ReadValidated(section, subsection, key, std::move(default_value),
                             [](const T &) { return true; });
    }

    template <config_detail::ConfigScalar T, typename Validator>
        requires config_detail::ConfigValidator<Validator, T>
    T Read(std::string_view section, std::string_view subsection,
           std::string_view key, T default_value, Validator validator) {
        return ReadValidated(section, subsection, key, std::move(default_value),
                             std::move(validator));
    }

    template <config_detail::ConfigScalar T>
    T Read(std::string_view section, std::string_view subsection,
           std::string_view key, T default_value, T minimum, T maximum) {
        return ReadValidated(
            section, subsection, key, std::move(default_value),
            [minimum, maximum](const T &value) { return value >= minimum && value <= maximum; });
    }

    template <config_detail::ConfigScalar T, typename Validator>
        requires config_detail::ConfigValidator<Validator, T>
    std::optional<T> TryRead(std::string_view section, std::string_view subsection,
                             std::string_view key, Validator validator) {
        nlohmann::json encoded_value;
        {
            std::scoped_lock lock(mutex_);
            const nlohmann::json *object = FindObjectLocked(section, subsection);
            if (!object || !object->contains(key)) return std::nullopt;
            encoded_value = object->at(key);
        }
        T result{};
        if (!config_detail::ReadJsonValue(encoded_value, result) ||
            !std::invoke(validator, std::as_const(result))) {
            return std::nullopt;
        }
        return result;
    }

    template <config_detail::ConfigScalar T>
    std::optional<T> TryRead(std::string_view section, std::string_view subsection,
                             std::string_view key, T minimum, T maximum) {
        return TryRead<T>(section, subsection, key, [minimum, maximum](const T &value) {
            return value >= minimum && value <= maximum;
        });
    }

    void EnsureObject(std::string_view section, std::string_view subsection);

#ifdef ARC_HELPER_HOST_TEST
    void SetRootDirForTesting(const std::string &root_dir);
    void ResetForTesting(const std::string &root_dir);
#endif

private:
    ConfigManager() = default;

    template <config_detail::ConfigScalar T, typename Validator>
        requires config_detail::ConfigValidator<Validator, T>
    T ReadValidated(std::string_view section, std::string_view subsection,
                    std::string_view key, T default_value, Validator validator) {
        nlohmann::json encoded_value;
        bool found_value = false;
        {
            std::scoped_lock lock(mutex_);
            nlohmann::json &object = GetObjectLocked(section, subsection);
            if (object.contains(key)) {
                encoded_value = object.at(key);
                found_value = true;
            }
        }

        T result{};
        const bool valid = found_value &&
                           config_detail::ReadJsonValue(encoded_value, result) &&
                           std::invoke(validator, std::as_const(result));
        if (!valid) {
            result = std::move(default_value);
            std::scoped_lock lock(mutex_);
            GetObjectLocked(section, subsection)[std::string(key)] = result;
        }
        return result;
    }

    nlohmann::json &GetObjectLocked(std::string_view section, std::string_view subsection);
    const nlohmann::json *FindObjectLocked(std::string_view section,
                                           std::string_view subsection) const;
    void SetRootDirLocked(const std::string &root_dir);

    mutable std::mutex mutex_{};
    nlohmann::json data_ = nlohmann::json::object();
    std::string package_name_{};
    std::string root_dir_{};
    std::string charts_dir_{};
    std::string cache_dir_{};
    std::string logs_dir_{};
    bool loaded_ = false;
};

} // namespace arc_helper
