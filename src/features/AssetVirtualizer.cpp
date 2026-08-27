#include "features/AssetVirtualizer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <android/asset_manager.h>

#include "config/CustomChartConfig.h"
#include "config/ModuleConfig.h"
#include "manager/CustomChartManager.hpp"
#include "manager/GameManager.hpp"
#include "manager/HookManager.hpp"
#include "manager/custom_chart/CustomChartGameplaySession.hpp"
#include <lsplt.hpp>
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_helper {
namespace {

struct VirtualAsset {
    std::shared_ptr<std::vector<uint8_t>> data;
    size_t position = 0;
    std::string logical_path;
};

struct VirtualDirectory {
    std::vector<std::string> entries;
    size_t position = 0;
};

std::mutex g_assets_mutex;
std::unordered_map<AAsset *, VirtualAsset> g_assets;
std::unordered_map<AAsset *, std::string> g_asset_paths;
std::unordered_map<AAssetDir *, std::unique_ptr<VirtualDirectory>> g_directories;
uintptr_t g_lib_base = 0;
cfg::CustomChartsOffsets g_offsets{};
mem::PatchTransaction g_songlist_patch_transaction;
mem::PatchTransaction g_scenecontrol_patch_transaction;

using OpenFn = AAsset *(*)(AAssetManager *, const char *, int);
using OpenDirFn = AAssetDir *(*)(AAssetManager *, const char *);
using NextFileNameFn = const char *(*)(AAssetDir *);
using CloseDirFn = void (*)(AAssetDir *);
using ReadFn = int (*)(AAsset *, void *, size_t);
using LengthFn = off_t (*)(AAsset *);
using CloseFn = void (*)(AAsset *);
using FindSongByIdFn = uintptr_t (*)(uintptr_t, const std::string *);

OpenFn g_open_original = nullptr;
OpenDirFn g_open_dir_original = nullptr;
NextFileNameFn g_next_file_name_original = nullptr;
CloseDirFn g_close_dir_original = nullptr;
ReadFn g_read_original = nullptr;
LengthFn g_length_original = nullptr;
CloseFn g_close_original = nullptr;
FindSongByIdFn g_find_song_by_id = nullptr;

bool ValidateInstallTargets() {
    if (!g_offsets.songlist_asset_loader_caller || !g_offsets.songlist_digest_size_guard ||
        !g_offsets.songlist_digest_compare_guard || !g_offsets.songlist_difficulty_filter ||
        !g_offsets.difficulty_availability ||
        !g_offsets.song_unlock_mask_check ||
        !g_offsets.content_availability ||
        !g_offsets.play_launcher ||
        !g_offsets.chart_path ||
        !g_offsets.song_registry_global || !g_offsets.find_song_by_id) {
        ARC_LOGE("Incomplete custom-chart profile");
        return false;
    }

    const uintptr_t loader_call = g_lib_base + g_offsets.songlist_asset_loader_caller - sizeof(uint32_t);
    const uintptr_t size_guard = g_lib_base + g_offsets.songlist_digest_size_guard;
    const uintptr_t compare_guard = g_lib_base + g_offsets.songlist_digest_compare_guard;
    if (!g_offsets.expected_songlist_loader_call ||
        !mem::ProcMaps::IsReadable(loader_call, sizeof(uint32_t)) ||
        !mem::ProcMaps::IsReadable(size_guard, sizeof(uint32_t)) ||
        !mem::ProcMaps::IsReadable(compare_guard, sizeof(uint32_t)) ||
        mem::Read<uint32_t>(loader_call) != g_offsets.expected_songlist_loader_call ||
        mem::Read<uint32_t>(size_guard) != cfg::custom_charts::kExpectedDigestSizeGuard ||
        mem::Read<uint32_t>(compare_guard) != cfg::custom_charts::kExpectedDigestCompareGuard) {
        ARC_LOGE("Install target signature mismatch");
        return false;
    }
    return true;
}

std::shared_ptr<std::vector<uint8_t>> ReadFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > cfg::custom_charts::kMaxVirtualAssetBytes) return {};
    auto data = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
    file.seekg(0);
    if (!data->empty() && !file.read(reinterpret_cast<char *>(data->data()), data->size())) return {};
    return data;
}

std::shared_ptr<std::vector<uint8_t>> ReadOfficialAsset(AAsset *asset) {
    if (!asset || !g_length_original || !g_read_original) return {};
    const off_t length = g_length_original(asset);
    if (length <= 0 || static_cast<uint64_t>(length) > cfg::custom_charts::kMaxOfficialAssetBytes) return {};
    auto data = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(length));
    size_t done = 0;
    while (done < data->size()) {
        const int count = g_read_original(asset, data->data() + done, data->size() - done);
        if (count <= 0) return {};
        done += static_cast<size_t>(count);
    }
    return data;
}

bool IsParserCaller(uintptr_t return_address) {
    if (!g_lib_base || !g_offsets.songlist_asset_loader_caller || return_address < g_lib_base) return false;
    const uintptr_t offset = return_address - g_lib_base;
    // 6.16.2c has two songlist open callers. 0xB5E244 only probes existence;
    // 0x142CFB0 is the caller that obtains length and reads the bytes.
    return offset == g_offsets.songlist_asset_loader_caller;
}

bool PatchSonglistDigestGuards() {
    const uintptr_t size_guard = g_lib_base + g_offsets.songlist_digest_size_guard;
    const uintptr_t compare_guard = g_lib_base + g_offsets.songlist_digest_compare_guard;
    const auto current_size = mem::RuntimeMemory::Process().Read<uint32_t>(size_guard);
    const auto current_compare = mem::RuntimeMemory::Process().Read<uint32_t>(compare_guard);
    if (!g_offsets.songlist_digest_size_guard || !g_offsets.songlist_digest_compare_guard ||
        !current_size || !current_compare ||
        *current_size != cfg::custom_charts::kExpectedDigestSizeGuard ||
        *current_compare != cfg::custom_charts::kExpectedDigestCompareGuard) {
        ARC_LOGE("Songlist digest guard signature mismatch");
        return false;
    }

    std::array<std::byte, sizeof(uint32_t)> expected_size{};
    std::array<std::byte, sizeof(uint32_t)> expected_compare{};
    std::array<std::byte, sizeof(uint32_t)> nop{};
    std::memcpy(expected_size.data(),
                &cfg::custom_charts::kExpectedDigestSizeGuard,
                expected_size.size());
    std::memcpy(expected_compare.data(),
                &cfg::custom_charts::kExpectedDigestCompareGuard,
                expected_compare.size());
    std::memcpy(nop.data(), &cfg::custom_charts::kNopInstruction, nop.size());
    const std::array<mem::PatchDescriptor, 2> patches = {{
        {size_guard,
         std::span<const std::byte>(expected_size),
         std::span<const std::byte>(nop)},
        {compare_guard,
         std::span<const std::byte>(expected_compare),
         std::span<const std::byte>(nop)},
    }};
    if (!g_songlist_patch_transaction.Apply(patches)) {
        if (g_songlist_patch_transaction.IsDegraded() &&
            !g_songlist_patch_transaction.Rollback()) {
            ARC_LOGE("Degraded digest rollback failed");
        }
        ARC_LOGE("Digest guard transaction failed");
        return false;
    }
    ARC_LOGI("Songlist digest guards patched");
    return true;
}

bool RestoreSonglistDigestGuards() {
    if (g_songlist_patch_transaction.State() == mem::PatchState::Ready ||
        g_songlist_patch_transaction.State() == mem::PatchState::RolledBack) {
        return true;
    }
    const bool restored = g_songlist_patch_transaction.Rollback().has_value();
    ARC_LOGI("Songlist digest rollback %s",
             restored ? "OK" : "FAILED");
    return restored;
}

// Forces the play-context "dynamic event chart" flag getter to return true so
// scenecontrol/timing commands drive camera/track effects for imported charts
// (see kExpectedScenecontrolGateGetter). Best-effort: a failure only leaves the
// effects dormant; older profiles without the offset skip the patch entirely.
bool PatchScenecontrolGate() {
    if (!g_offsets.scenecontrol_gate_getter) {
        ARC_LOGI("No scenecontrol gate offset for this profile");
        return true;
    }
    const uintptr_t getter = g_lib_base + g_offsets.scenecontrol_gate_getter;
    constexpr size_t kGateBytes = cfg::custom_charts::kExpectedScenecontrolGateGetter.size();
    if (!mem::ProcMaps::IsReadable(getter, kGateBytes)) {
        ARC_LOGE("Scenecontrol gate not readable at %p", reinterpret_cast<void *>(getter));
        return false;
    }
    std::array<std::byte, kGateBytes> expected{};
    std::array<std::byte, kGateBytes> replacement{};
    std::memcpy(expected.data(),
                cfg::custom_charts::kExpectedScenecontrolGateGetter.data(),
                expected.size());
    std::memcpy(replacement.data(),
                cfg::custom_charts::kScenecontrolGateAlwaysTrue.data(),
                replacement.size());
    const std::array<mem::PatchDescriptor, 1> patches = {{
        {getter,
         std::span<const std::byte>(expected),
         std::span<const std::byte>(replacement)},
    }};
    if (!g_scenecontrol_patch_transaction.Apply(patches)) {
        if (g_scenecontrol_patch_transaction.IsDegraded() &&
            !g_scenecontrol_patch_transaction.Rollback()) {
            ARC_LOGE("Degraded scenecontrol rollback failed");
        }
        ARC_LOGE("Scenecontrol gate transaction failed");
        return false;
    }
    ARC_LOGI("Scenecontrol gate patched");
    return true;
}

bool RestoreScenecontrolGate() {
    if (g_scenecontrol_patch_transaction.State() == mem::PatchState::Ready ||
        g_scenecontrol_patch_transaction.State() == mem::PatchState::RolledBack) {
        return true;
    }
    const bool restored = g_scenecontrol_patch_transaction.Rollback().has_value();
    ARC_LOGI("Scenecontrol gate rollback %s",
             restored ? "OK" : "FAILED");
    return restored;
}

bool RestoreAssetHooks(dev_t dev, ino_t inode) {
    bool queued = false;
    bool registered = true;
    const auto restore = [&](const char *symbol, auto original) {
        if (!original) return;
        queued = true;
        registered = lsplt::RegisterHook(dev, inode, symbol, reinterpret_cast<void *>(original), nullptr) &&
                     registered;
    };
    restore("AAssetManager_open", g_open_original);
    restore("AAssetManager_openDir", g_open_dir_original);
    restore("AAssetDir_getNextFileName", g_next_file_name_original);
    restore("AAssetDir_close", g_close_dir_original);
    restore("AAsset_read", g_read_original);
    restore("AAsset_getLength", g_length_original);
    restore("AAsset_close", g_close_original);
    const bool committed = !queued || lsplt::CommitHook();
    const bool restored = registered && committed;
    ARC_LOGI("PLT rollback %s", restored ? "OK" : "FAILED");
    return restored;
}

struct RuntimeSongDifficultyPair {
    uintptr_t song = 0;
    uint32_t difficulty = 0;
    uint32_t padding = 0;
};

struct RuntimeSongDifficultyList {
    RuntimeSongDifficultyPair *begin = nullptr;
    RuntimeSongDifficultyPair *end = nullptr;
    RuntimeSongDifficultyPair *capacity = nullptr;
};

static_assert(sizeof(RuntimeSongDifficultyPair) == 16);
static_assert(sizeof(RuntimeSongDifficultyList) == 24);

bool IsValidRuntimeList(const RuntimeSongDifficultyList &list) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(list.begin);
    const uintptr_t end = reinterpret_cast<uintptr_t>(list.end);
    const uintptr_t capacity = reinterpret_cast<uintptr_t>(list.capacity);
    if (!begin) return !end && !capacity;
    if (end < begin || capacity < end) return false;
    if ((end - begin) % sizeof(RuntimeSongDifficultyPair) != 0 ||
        (capacity - begin) % sizeof(RuntimeSongDifficultyPair) != 0) {
        return false;
    }
    const size_t count = static_cast<size_t>((end - begin) / sizeof(RuntimeSongDifficultyPair));
    const size_t capacity_count = static_cast<size_t>((capacity - begin) /
                                                       sizeof(RuntimeSongDifficultyPair));
    return count <= cfg::custom_charts::kMaxRuntimeDifficultyPairs &&
           capacity_count <= cfg::custom_charts::kMaxRuntimeDifficultyPairs * 2 &&
           (count == 0 || mem::ProcMaps::IsReadable(begin, count * sizeof(RuntimeSongDifficultyPair)));
}

bool ReadSongId(uintptr_t song, std::string_view &out) {
    if (!song || !mem::ProcMaps::IsReadable(song, 24)) return false;
    const auto *raw = reinterpret_cast<const uint8_t *>(song);
    const char *data = nullptr;
    size_t size = 0;
    if ((raw[0] & 1) == 0) {
        size = raw[0] >> 1;
        data = reinterpret_cast<const char *>(raw + 1);
    } else {
        std::memcpy(&size, raw + 8, sizeof(size));
        std::memcpy(&data, raw + 16, sizeof(data));
        if (!data || size == 0 ||
            !mem::ProcMaps::IsReadable(reinterpret_cast<uintptr_t>(data), size)) {
            return false;
        }
    }
    if (size == 0 || size > cfg::custom_charts::kMaxSanitizedIdLength) return false;
    out = std::string_view(data, size);
    return true;
}

bool IsCustomRuntimeSong(uintptr_t song) {
    std::string_view id;
    return ReadSongId(song, id) && CustomChartManager::Instance().ContainsSongId(id);
}

bool ContainsRuntimeSong(const RuntimeSongDifficultyList &list, uintptr_t song) {
    if (!list.begin) return false;
    return std::any_of(list.begin, list.end, [song](const auto &item) { return item.song == song; });
}

bool ContainsCustomRuntimeSong(const RuntimeSongDifficultyList &list) {
    if (!list.begin) return false;
    return std::any_of(list.begin, list.end, [](const auto &item) {
        return IsCustomRuntimeSong(item.song);
    });
}

uintptr_t FindRuntimeSong(std::string_view song_id) {
    if (!g_find_song_by_id) return 0;
    const uintptr_t owner_slot = g_lib_base + g_offsets.song_registry_global;
    if (!mem::ProcMaps::IsReadable(owner_slot, sizeof(uintptr_t))) return 0;
    const uintptr_t owner = mem::Read<uintptr_t>(owner_slot);
    if (!owner || !mem::ProcMaps::IsReadable(
                     owner + cfg::custom_charts::kSongRegistryOwnerRegistryOffset,
                     sizeof(uintptr_t))) {
        return 0;
    }
    const uintptr_t registry = mem::Read<uintptr_t>(
        owner + cfg::custom_charts::kSongRegistryOwnerRegistryOffset);
    if (!registry) return 0;
    const std::string id(song_id);
    return g_find_song_by_id(registry, &id);
}

bool IsValidDifficultyObject(uintptr_t difficulty_object) {
    return difficulty_object && (difficulty_object & (alignof(uintptr_t) - 1)) == 0 &&
           mem::ProcMaps::IsReadable(
               difficulty_object, cfg::custom_charts::kDifficultyObjectReadableBytes);
}

void TrackAssetPath(AAsset *asset, std::string_view logical_path) {
    if (!asset) return;
    std::scoped_lock lock(g_assets_mutex);
    g_asset_paths[asset] = std::string(logical_path);
}

bool UnlockCustomDifficulty(uintptr_t song, unsigned int requested_difficulty) {
    if (!IsCustomRuntimeSong(song) ||
        requested_difficulty >= cfg::custom_charts::kDifficultyCount) {
        return false;
    }

    const uintptr_t slot = song + cfg::custom_charts::kDifficultyPointersOffset +
                           requested_difficulty * sizeof(uintptr_t);
    const uintptr_t difficulty_object = mem::Read<uintptr_t>(slot);
    if (!IsValidDifficultyObject(difficulty_object)) return false;

    const uintptr_t lock = difficulty_object + cfg::custom_charts::kDifficultyLockOffset;
    if (lock < difficulty_object || !mem::ProcMaps::IsWritable(lock, sizeof(uint8_t))) {
        ARC_LOGE("Custom difficulty lock field is not writable for %p",
                 reinterpret_cast<void *>(song));
        return false;
    }
    const auto current = mem::RuntimeMemory::Process().Read<uint8_t>(lock);
    if (!current) return false;
    if (*current == 0) return true;
    if (!mem::RuntimeMemory::Process().Write<uint8_t>(lock, 0)) {
        ARC_LOGE("Failed to clear custom difficulty lock for %p class %u",
                 reinterpret_cast<void *>(song),
                 requested_difficulty);
        return false;
    }
    ARC_LOGI("Cleared custom difficulty lock for %p class %u",
             reinterpret_cast<void *>(song),
             requested_difficulty);
    return true;
}

uint64_t DifficultyAvailabilityHook(uintptr_t song, uint64_t difficulty) {
    if (IsCustomRuntimeSong(song)) {
        std::string_view id;
        if (difficulty >= cfg::custom_charts::kDifficultyCount || !ReadSongId(song, id)) {
            return CALL_ORIG(DifficultyAvailabilityHook, song, difficulty);
        }
        const std::string path = cfg::custom_charts::LocalChartAssetPath(id, difficulty);
        if (!CustomChartManager::Instance().ResolveAsset(path)) return 0;
        if (difficulty == cfg::custom_charts::kBeyondDifficulty &&
            UnlockCustomDifficulty(song, static_cast<unsigned int>(difficulty))) {
            return 1;
        }
    }
    return CALL_ORIG(DifficultyAvailabilityHook, song, difficulty);
}

uint64_t SongUnlockMaskCheckHook(uintptr_t state, uintptr_t song) {
    const uint64_t unlocked = CALL_ORIG(SongUnlockMaskCheckHook, state, song);
    return unlocked || IsCustomRuntimeSong(song);
}

bool CustomDifficultyHasChart(uintptr_t song, uint32_t difficulty) {
    std::string_view id;
    if (difficulty >= cfg::custom_charts::kDifficultyCount || !ReadSongId(song, id)) {
        return false;
    }
    return CustomChartManager::Instance().ResolveAsset(
               cfg::custom_charts::LocalChartAssetPath(id, difficulty)) != nullptr;
}

// song+0x1C0 is a song-level "remote download pack" bit. Preview BGM
// (sub_9BCE10) prefixes the id with dl_ when it is set, and FMOD then
// throws AudioProvider_error on the missing file. The play launcher
// (C987A8) also uses it, together with ContentAvailability==1, to take
// loc_C9940C immediately for PST/PRS placeholders. Raise the bit only
// for that call and put the previous value back before returning.
class ScopedCustomRemotePackFlag {
public:
    explicit ScopedCustomRemotePackFlag(uintptr_t song) {
        if (!song) return;
        const uintptr_t flag = song + cfg::custom_charts::kRemotePackFlagOffset;
        if (flag < song || !mem::ProcMaps::IsWritable(flag, sizeof(uint8_t))) return;
        const auto current = mem::RuntimeMemory::Process().Read<uint8_t>(flag);
        if (!current) return;
        saved_ = *current;
        addr_ = flag;
        if (saved_ != 1 &&
            !mem::RuntimeMemory::Process().Write<uint8_t>(flag, 1).has_value()) {
            addr_ = 0;
            return;
        }
        active_ = true;
    }

    ~ScopedCustomRemotePackFlag() {
        if (!active_ || !addr_) return;
        if (saved_ != 1) {
            mem::RuntimeMemory::Process().Write<uint8_t>(addr_, saved_);
        }
    }

    ScopedCustomRemotePackFlag(const ScopedCustomRemotePackFlag &) = delete;
    ScopedCustomRemotePackFlag &operator=(const ScopedCustomRemotePackFlag &) = delete;

private:
    uintptr_t addr_ = 0;
    uint8_t saved_ = 0;
    bool active_ = false;
};

uint64_t ContentAvailabilityHook(uintptr_t song,
                                 uint32_t difficulty,
                                 uint32_t state,
                                 uint32_t flags) {
    // Playable custom charts return 0 so SongCell keeps start.png and the
    // play launcher continues into the local chart. Placeholders return 1
    // so Beyond (class 3) takes loc_C9940C. PST/PRS placeholders get that
    // same dialog from PlayLauncherHook without leaving song+0x1C0 set.
    if (!IsCustomRuntimeSong(song)) {
        return CALL_ORIG(ContentAvailabilityHook, song, difficulty, state, flags);
    }
    if (CustomDifficultyHasChart(song, difficulty)) return 0;
    return 1;
}

void PlayLauncherHook(uintptr_t context,
                      uintptr_t song,
                      uint32_t difficulty,
                      uintptr_t extra,
                      uint32_t flags,
                      uintptr_t extra2) {
    const bool placeholder = IsCustomRuntimeSong(song) &&
                             !CustomDifficultyHasChart(song, difficulty);
    const ScopedCustomRemotePackFlag dialog_gate(placeholder ? song : 0);
    CALL_ORIG(PlayLauncherHook, context, song, difficulty, extra, flags, extra2);
}

std::string ChartPathHook(uintptr_t song, uint32_t difficulty) {
    // Official songs, including official Beyond packs, always use the
    // original builder. Membership is the import-success id set. The
    // rewritten path is emitted only when that difficulty was imported.
    std::string_view id;
    if (!IsCustomRuntimeSong(song) ||
        difficulty >= cfg::custom_charts::kDifficultyCount ||
        !ReadSongId(song, id)) {
        return CALL_ORIG(ChartPathHook, song, difficulty);
    }
    std::string path = cfg::custom_charts::LocalChartAssetPath(id, difficulty);
    if (!CustomChartManager::Instance().ResolveAsset(path)) {
        return CALL_ORIG(ChartPathHook, song, difficulty);
    }
    ARC_LOGI("Custom chart path %s", path.c_str());
    return path;
}

bool NormalizeMissingDifficultySlots(uintptr_t song, unsigned int requested_difficulty) {
    if (requested_difficulty >= cfg::custom_charts::kDifficultyCount ||
        !mem::ProcMaps::IsReadable(
            song,
            cfg::custom_charts::kDifficultyPresenceOffset + cfg::custom_charts::kDifficultyCount)) {
        return false;
    }

    const uintptr_t pointer_base = song + cfg::custom_charts::kDifficultyPointersOffset;
    if (pointer_base < song ||
        !mem::ProcMaps::IsWritable(pointer_base,
                                    cfg::custom_charts::kDifficultyCount * sizeof(uintptr_t))) {
        return false;
    }

    const uintptr_t requested_presence = song + cfg::custom_charts::kDifficultyPresenceOffset +
                                         requested_difficulty;
    if (mem::Read<uint8_t>(requested_presence) == 0) return false;

    const uintptr_t requested_slot = song + cfg::custom_charts::kDifficultyPointersOffset +
                                     requested_difficulty * sizeof(uintptr_t);
    const uintptr_t fallback = mem::Read<uintptr_t>(requested_slot);
    if (!IsValidDifficultyObject(fallback)) return false;

    size_t normalized = 0;
    for (size_t difficulty = 0; difficulty < cfg::custom_charts::kDifficultyCount; ++difficulty) {
        const bool present = mem::Read<uint8_t>(
                                  song + cfg::custom_charts::kDifficultyPresenceOffset + difficulty) != 0;
        const uintptr_t slot = song + cfg::custom_charts::kDifficultyPointersOffset +
                               difficulty * sizeof(uintptr_t);
        const uintptr_t value = mem::Read<uintptr_t>(slot);
        if (present) {
            if (!IsValidDifficultyObject(value)) return false;
            continue;
        }
        if (value == fallback) continue;
        if (!mem::RuntimeMemory::Process().Write<uintptr_t>(slot, fallback)) return false;
        ++normalized;
    }
    if (normalized) {
        ARC_LOGI("Normalized %zu missing difficulty slots for %p using class %u",
                 normalized,
                 reinterpret_cast<void *>(song),
                 requested_difficulty);
    }
    return true;
}

RuntimeSongDifficultyList SonglistDifficultyFilterHook(void *context,
                                                       unsigned int mode,
                                                       void *song_ids,
                                                       unsigned int difficulty,
                                                       const void *group) {
    auto result = CALL_ORIG(SonglistDifficultyFilterHook, context, mode, song_ids, difficulty, group);
    if (difficulty >= static_cast<unsigned int>(cfg::custom_charts::kDifficultyCount) ||
        !IsValidRuntimeList(result)) {
        return result;
    }

    bool custom_scope = ContainsCustomRuntimeSong(result) ||
                        CustomChartManager::Instance().HasSongs();
    if (!custom_scope) return result;

    std::vector<RuntimeSongDifficultyPair> additions;
    for (const auto &song_id :
         CustomChartManager::Instance().ListSongIdsForDifficulty(static_cast<int>(difficulty))) {
        const uintptr_t song = FindRuntimeSong(song_id);
        if (!song || !IsCustomRuntimeSong(song) || ContainsRuntimeSong(result, song)) continue;
        if (!NormalizeMissingDifficultySlots(song, difficulty)) {
            ARC_LOGE("Invalid runtime difficulty layout for %s class %u",
                     song_id.c_str(),
                     difficulty);
            continue;
        }
        // In 6.16.2c the local-lock decision is made from difficulty +0xF0;
        // the song-level byd_local_unlock JSON field is not consulted here.
        if (!UnlockCustomDifficulty(song, difficulty)) {
            ARC_LOGE("Unable to unlock custom difficulty for %s class %u",
                     song_id.c_str(),
                     difficulty);
            continue;
        }
        additions.push_back({song, difficulty, 0});
    }
    if (additions.empty()) return result;

    const size_t old_count = result.begin ? static_cast<size_t>(result.end - result.begin) : 0;
    if (additions.size() > std::numeric_limits<size_t>::max() - old_count ||
        old_count + additions.size() > cfg::custom_charts::kMaxRuntimeDifficultyPairs) {
        ARC_LOGE("Runtime difficulty list capacity exceeded");
        return result;
    }
    const size_t new_count = old_count + additions.size();
    auto *merged = static_cast<RuntimeSongDifficultyPair *>(
        ::operator new(new_count * sizeof(RuntimeSongDifficultyPair)));
    if (old_count) std::memcpy(merged, result.begin, old_count * sizeof(*merged));
    std::copy(additions.begin(), additions.end(), merged + old_count);
    ::operator delete(result.begin);
    result = {merged, merged + new_count, merged + new_count};
    ARC_LOGI("Added %zu custom songs for difficulty %u",
             additions.size(), difficulty);
    return result;
}

void FmodLoadBgmHook(void *provider, const char *path, int channel) {
    if (path) {
        const auto *source = CustomChartManager::Instance().ResolveAsset(path);
        if (source && !source->starts_with(cfg::custom_charts::kOfficialAssetPrefix)) {
            ARC_LOGI("FMOD remapped %s -> %s", path, source->c_str());
            CALL_ORIG(FmodLoadBgmHook, provider, source->c_str(), channel);
            return;
        }
    }
    CALL_ORIG(FmodLoadBgmHook, provider, path, channel);
}

AAsset *OpenHook(AAssetManager *manager, const char *filename, int mode) {
    if (!g_open_original) return nullptr;
    if (!filename) return g_open_original(manager, filename, mode);

    ARC_LOGD("AAssetManager_open %s", filename);

    const std::string_view path(filename);
    auto &charts = CustomChartManager::Instance();

    if (path == cfg::custom_charts::kSonglistAssetPath) {
        AAsset *asset = g_open_original(manager, filename, mode);
        if (!asset || !IsParserCaller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))) {
            TrackAssetPath(asset, path);
            return asset;
        }
        const auto official = ReadOfficialAsset(asset);
        if (!official) {
            ARC_LOGE("Failed to read official songlist");
            g_close_original(asset);
            asset = g_open_original(manager, filename, mode);
            TrackAssetPath(asset, path);
            return asset;
        }
        std::string error;
        const std::string merged = charts.MergeSonglist(
            std::string_view(reinterpret_cast<const char *>(official->data()), official->size()), error);
        if (merged.empty()) {
            ARC_LOGE("Songlist merge failed: %s", error.c_str());
            g_close_original(asset);
            asset = g_open_original(manager, filename, mode);
            TrackAssetPath(asset, path);
            return asset;
        }
        auto bytes = std::make_shared<std::vector<uint8_t>>(merged.begin(), merged.end());
        {
            std::scoped_lock lock(g_assets_mutex);
            g_assets[asset] = {std::move(bytes), 0, std::string(path)};
            g_asset_paths[asset] = std::string(path);
        }
        ARC_LOGI("Serving merged songlist (%zu -> %zu bytes)", official->size(), merged.size());
        return asset;
    }

    const std::string *source = charts.ResolveAsset(path);
    if (!source) {
        AAsset *asset = g_open_original(manager, filename, mode);
        TrackAssetPath(asset, path);
        return asset;
    }

    if (source->starts_with(cfg::custom_charts::kOfficialAssetPrefix)) {
        AAsset *asset = g_open_original(manager,
                                        source->c_str() + cfg::custom_charts::kOfficialAssetPrefix.size(),
                                        mode);
        TrackAssetPath(asset, path);
        return asset;
    }

    const auto bytes = ReadFile(*source);
    if (!bytes) {
        ARC_LOGE("Failed to read %s -> %s", filename, source->c_str());
        return nullptr;
    }
    AAsset *asset = g_open_original(manager, cfg::custom_charts::kSonglistAssetPath.data(), mode);
    if (!asset) {
        return nullptr;
    }
    {
        std::scoped_lock lock(g_assets_mutex);
        g_assets[asset] = {bytes, 0, std::string(path)};
        g_asset_paths[asset] = std::string(path);
    }
    if (charts.IsCustomChartPath(path)) {
        CustomChartGameplaySession::Instance().OnCustomChartMapped(path);
    }
    ARC_LOGI("Mapped %s (%zu bytes)", filename, bytes->size());
    return asset;
}

AAssetDir *OpenDirHook(AAssetManager *manager, const char *dirname) {
    if (!g_open_dir_original) return nullptr;
    if (!dirname) return g_open_dir_original(manager, dirname);

    auto entries = CustomChartManager::Instance().ListAssetDirectory(dirname);
    if (entries.empty()) return g_open_dir_original(manager, dirname);

    auto directory = std::make_unique<VirtualDirectory>();
    directory->entries = std::move(entries);
    auto *handle = reinterpret_cast<AAssetDir *>(directory.get());
    {
        std::scoped_lock lock(g_assets_mutex);
        g_directories.emplace(handle, std::move(directory));
    }
    ARC_LOGI("Mapped directory %s", dirname);
    return handle;
}

const char *NextFileNameHook(AAssetDir *asset_dir) {
    {
        std::scoped_lock lock(g_assets_mutex);
        const auto it = g_directories.find(asset_dir);
        if (it != g_directories.end()) {
            auto &directory = *it->second;
            if (directory.position >= directory.entries.size()) return nullptr;
            return directory.entries[directory.position++].c_str();
        }
    }
    return g_next_file_name_original ? g_next_file_name_original(asset_dir) : nullptr;
}

void CloseDirHook(AAssetDir *asset_dir) {
    bool virtual_directory = false;
    {
        std::scoped_lock lock(g_assets_mutex);
        virtual_directory = g_directories.erase(asset_dir) != 0;
    }
    if (virtual_directory) return;
    if (g_close_dir_original) g_close_dir_original(asset_dir);
}

int ReadHook(AAsset *asset, void *buffer, size_t count) {
    std::string logical_path;
    bool virtual_asset = false;
    int virtual_result = -1;
    {
        std::scoped_lock lock(g_assets_mutex);
        const auto it = g_assets.find(asset);
        if (it != g_assets.end()) {
            virtual_asset = true;
            logical_path = it->second.logical_path;
            if (buffer && it->second.data) {
                const size_t remaining = it->second.position < it->second.data->size()
                                             ? it->second.data->size() - it->second.position : 0;
                const size_t n = std::min(count, remaining);
                if (n == 0 ||
                    mem::ProcMaps::IsWritable(reinterpret_cast<uintptr_t>(buffer), n)) {
                    if (n) std::memcpy(buffer, it->second.data->data() + it->second.position, n);
                    it->second.position += n;
                    virtual_result = static_cast<int>(n);
                }
            }
        } else if (const auto path_it = g_asset_paths.find(asset); path_it != g_asset_paths.end()) {
            logical_path = path_it->second;
        }
    }
    if (virtual_asset) {
        CustomChartGameplaySession::Instance().OnAssetRead(logical_path);
        return virtual_result;
    }
    const int result = g_read_original ? g_read_original(asset, buffer, count) : -1;
    if (!logical_path.empty()) {
        CustomChartGameplaySession::Instance().OnAssetRead(logical_path);
    }
    return result;
}

off_t LengthHook(AAsset *asset) {
    {
        std::scoped_lock lock(g_assets_mutex);
        const auto it = g_assets.find(asset);
        if (it != g_assets.end() && it->second.data)
            return static_cast<off_t>(it->second.data->size());
    }
    return g_length_original ? g_length_original(asset) : 0;
}

void CloseHook(AAsset *asset) {
    {
        std::scoped_lock lock(g_assets_mutex);
        g_assets.erase(asset);
        g_asset_paths.erase(asset);
    }
    if (g_close_original) g_close_original(asset);
}

} // namespace

AssetVirtualizer &AssetVirtualizer::Instance() {
    static AssetVirtualizer virtualizer;
    return virtualizer;
}

bool AssetVirtualizer::Install(const cfg::GameProfile &profile) {
    if (installed_) return true;
    lib_base_ = GameManager::Instance().GetOrFindGameLibBase();
    if (!lib_base_) return false;
    g_lib_base = lib_base_;
    offsets_ = profile.custom_charts;
    g_offsets = offsets_;
    cfg::SetRuntimeLayoutVersion(profile.id);
    if (!ValidateInstallTargets()) return false;

    dev_t dev = 0;
    ino_t inode = 0;
    for (const auto &map : lsplt::MapInfo::Scan()) {
        if (map.path.ends_with(cfg::module::kLibName)) {
            dev = map.dev;
            inode = map.inode;
        }
    }
    if (!inode) {
        ARC_LOGE("Target inode not found");
        return false;
    }

    const bool open_registered =
        lsplt::RegisterHook(dev, inode, "AAssetManager_open", reinterpret_cast<void *>(OpenHook),
                            reinterpret_cast<void **>(&g_open_original));
    const bool open_dir_registered =
        lsplt::RegisterHook(dev, inode, "AAssetManager_openDir", reinterpret_cast<void *>(OpenDirHook),
                            reinterpret_cast<void **>(&g_open_dir_original));
    const bool next_file_name_registered =
        lsplt::RegisterHook(dev, inode, "AAssetDir_getNextFileName",
                            reinterpret_cast<void *>(NextFileNameHook),
                            reinterpret_cast<void **>(&g_next_file_name_original));
    const bool close_dir_registered =
        lsplt::RegisterHook(dev, inode, "AAssetDir_close", reinterpret_cast<void *>(CloseDirHook),
                            reinterpret_cast<void **>(&g_close_dir_original));
    const bool read_registered =
        lsplt::RegisterHook(dev, inode, "AAsset_read", reinterpret_cast<void *>(ReadHook),
                            reinterpret_cast<void **>(&g_read_original));
    const bool length_registered =
        lsplt::RegisterHook(dev, inode, "AAsset_getLength", reinterpret_cast<void *>(LengthHook),
                            reinterpret_cast<void **>(&g_length_original));
    const bool close_registered =
        lsplt::RegisterHook(dev, inode, "AAsset_close", reinterpret_cast<void *>(CloseHook),
                            reinterpret_cast<void **>(&g_close_original));
    const bool registered = open_registered && open_dir_registered && next_file_name_registered &&
                            close_dir_registered && read_registered && length_registered && close_registered;
    if (!registered) {
        // Flush any successfully queued subset so its original pointers become
        // available, then immediately restore it.
        lsplt::CommitHook();
        RestoreAssetHooks(dev, inode);
        ARC_LOGE("PLT hook registration failed");
        return false;
    }
    const bool committed = lsplt::CommitHook();
    const bool plt_ready = committed && g_open_original && g_open_dir_original &&
                           g_next_file_name_original && g_close_dir_original && g_read_original &&
                           g_length_original && g_close_original;
    if (!plt_ready) {
        RestoreAssetHooks(dev, inode);
        ARC_LOGE("PLT hook commit failed");
        return false;
    }

    uintptr_t songlist_difficulty_filter = 0;
    uintptr_t difficulty_availability = 0;
    uintptr_t song_unlock_mask_check = 0;
    uintptr_t content_availability = 0;
    uintptr_t play_launcher = 0;
    uintptr_t chart_path = 0;
    uintptr_t fmod_load_bgm = 0;
    auto &hook_manager = HookManager::Instance();
    uintptr_t find_song_by_id = 0;
    hook_manager.ResolveFunctionPtr(find_song_by_id,
                                    offsets_.find_song_by_id,
                                    cfg::custom_charts::kSigFindSongById,
                                    g_find_song_by_id,
                                    "song registry lookup");
    if (!g_find_song_by_id) {
        RestoreAssetHooks(dev, inode);
        return false;
    }
    std::array<HookManager::InlineHookRegistration, 7> registrations = {
        hook_manager.RegisterInlineHookSymbol(fmod_load_bgm,
                                              cfg::custom_charts::kFmodProviderLibrary,
                                              cfg::custom_charts::kFmodLoadBgmSymbol,
                                              cfg::custom_charts::kSigFmodLoadBgm,
                                              FmodLoadBgmHook,
                                              "AudioProviderFMODAndroid::loadBGM"),
        hook_manager.RegisterInlineHook(songlist_difficulty_filter,
                                        offsets_.songlist_difficulty_filter,
                                        cfg::custom_charts::kSigSonglistDifficultyFilter,
                                        SonglistDifficultyFilterHook,
                                        "songlist difficulty filter"),
        hook_manager.RegisterInlineHook(difficulty_availability,
                                        offsets_.difficulty_availability,
                                        cfg::custom_charts::kSigDifficultyAvailability,
                                        DifficultyAvailabilityHook,
                                        "difficulty availability"),
        hook_manager.RegisterInlineHook(song_unlock_mask_check,
                                        offsets_.song_unlock_mask_check,
                                        cfg::custom_charts::kSigSongUnlockMaskCheck,
                                        SongUnlockMaskCheckHook,
                                        "song unlock mask check"),
        hook_manager.RegisterInlineHook(content_availability,
                                        offsets_.content_availability,
                                        cfg::custom_charts::kSigContentAvailability,
                                        ContentAvailabilityHook,
                                        "content availability"),
        hook_manager.RegisterInlineHook(play_launcher,
                                        offsets_.play_launcher,
                                        cfg::custom_charts::kSigPlayLauncher,
                                        PlayLauncherHook,
                                        "play launcher"),
        hook_manager.RegisterInlineHook(chart_path,
                                        offsets_.chart_path,
                                        cfg::custom_charts::kSigChartPath,
                                        ChartPathHook,
                                        "chart path"),
    };
    if (std::ranges::any_of(registrations, [](const auto &registration) {
            return !registration;
        })) {
        ARC_LOGE("Inline hook registration failed");
        RestoreAssetHooks(dev, inode);
        return false;
    }
    if (!PatchSonglistDigestGuards()) {
        RestoreAssetHooks(dev, inode);
        return false;
    }
    if (!PatchScenecontrolGate()) {
        ARC_LOGI("Continuing without scenecontrol/timing effects");
    }
    if (!hook_manager.CommitInlineHook(
            std::span<HookManager::InlineHookRegistration>(registrations))) {
        RestoreSonglistDigestGuards();
        RestoreScenecontrolGate();
        RestoreAssetHooks(dev, inode);
        return false;
    }

    installed_ = true;
    ARC_LOGI("Hook install OK");
    return installed_;
}

} // namespace arc_helper
