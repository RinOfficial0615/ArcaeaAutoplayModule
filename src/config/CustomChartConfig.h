#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

#include "game/GameStructs.hpp"

namespace arc_helper::cfg::custom_charts {

// Bounded parser values. User-facing defaults are owned by CustomCharts.
// Song side values come straight from official songlist data, which has been
// widening the enum: 2 since v4.0 (epilogue), 3 since v6.0 (lephon/designant),
// and 4 first in 7.0.0c (konzetsu). Anything outside this domain falls back to
// the configured default.
inline constexpr double kMinimumBpm = 1.0;
inline constexpr double kMaximumBpm = 10000.0;
inline constexpr int kMinimumSide = 0;
inline constexpr int kMaximumSide = 4;
inline constexpr const char *kLightBackground = "base_light";
inline constexpr const char *kConflictBackground = "base_conflict";
inline constexpr double kDefaultChartConstant = -1.0;
inline constexpr int64_t kMaximumPreviewEndMs = INT32_MAX;
inline constexpr int64_t kDefaultPreviewDurationMs = 30000;
inline constexpr int kMinimumRating = 0;
inline constexpr int kMaximumRating = 20;
inline constexpr int kPlaceholderRating = -1;
// Display-class echo (`ratingClassAlias`). Officially observed values are
// 0/absent and 1 (Inscribed styling on a ratingClass-3 slot); the bound
// mirrors the difficulty-class domain the game funnels through
// `sub_1194380`.
inline constexpr int kMinimumRatingClassAlias = 0;
inline constexpr int kMaximumRatingClassAlias = 4;
inline constexpr int kMinimumChartConstant = 0;
inline constexpr int kMaxAffIncludeDepth = 8;

inline constexpr size_t kDifficultyCount = layouts::kSongDifficultySlotCount;
inline constexpr size_t kPastDifficulty = 0;
inline constexpr size_t kPresentDifficulty = 1;
inline constexpr size_t kFutureDifficulty = 2;
inline constexpr size_t kBeyondDifficulty = 3;
inline constexpr size_t kEternalDifficulty = 4;
inline constexpr size_t kSongIdHashChars = 8;
inline constexpr size_t kMaxSongIdLength = 21;
inline constexpr size_t kMaxSanitizedIdLength = 48;
inline constexpr size_t kMaxTextEntryBytes = 8 * 1024 * 1024;
inline constexpr size_t kMaxMetadataStringBytes = 256;
inline constexpr uint64_t kMaxVirtualAssetBytes = 128ull * 1024 * 1024;
inline constexpr uint64_t kMaxOfficialAssetBytes = 64ull * 1024 * 1024;

// Offsets computed from `layouts::*` mirror structs (see `GameStructs.hpp`).
// Confirmed shared through 6.16.8c, and re-verified on 7.0.0c against the
// matched chart-path/play-launcher bodies (`song+0x1C0`, slot table at
// `song+0x228`, lock at diff+0xF0).
constexpr GameVersionId kLayoutVer = GameVersionId::k6162c;
inline constexpr size_t kDifficultyPointersOffset =
    offsetof(layouts::Song<kLayoutVer>, difficulty_pointers);
inline constexpr size_t kDifficultyPresenceOffset =
    offsetof(layouts::Song<kLayoutVer>, difficulty_presence);
inline constexpr size_t kRemotePackFlagOffset =
    offsetof(layouts::Song<kLayoutVer>, remote_pack);
inline constexpr int kOfficialBackgroundWidth = 1920;
inline constexpr int kOfficialBackgroundHeight = 1440;
inline constexpr size_t kDifficultyLockOffset =
    offsetof(layouts::SongDifficulty<kLayoutVer>, lock);
inline constexpr size_t kDifficultyObjectReadableBytes =
    sizeof(layouts::SongDifficulty<kLayoutVer>);
inline constexpr size_t kSongRegistryOwnerRegistryOffset =
    offsetof(layouts::SongRegistryOwner<kLayoutVer>, registry);
inline constexpr size_t kMaxRuntimeDifficultyPairs = 4096;

// Asset namespaces and importer-generated aliases.
inline constexpr std::string_view kSongsPrefix = "songs/";
inline constexpr std::string_view kSonglistAssetPath = "songs/songlist";
inline constexpr std::string_view kOfficialAssetPrefix = "@official:";
inline constexpr std::string_view kCustomSongIdPrefix = "ah_";
inline constexpr std::string_view kCustomBackgroundPrefix = "ahbg_";
inline constexpr std::string_view kBackgroundAssetPrefix = "img/bg/1080/";
inline constexpr std::array<std::string_view, 3> kAssetPathPrefixes = {
    "file:///android_asset/", "Resources/", "assets/",
};
inline constexpr std::array<std::string_view, 2> kAudioFileNames = {"base.ogg", "base.wav"};
inline constexpr std::array<std::string_view, 3> kJacketFileNames = {
    "base.jpg", "base.png", "base.jpeg",
};
inline constexpr const char *kAudioAssetName = "base.ogg";
inline constexpr const char *kJacketAssetName = "base.jpg";
inline constexpr const char *kJacket256AssetName = "base_256.jpg";

// Extension sets shared by the importer's asset discovery. Array order is the
// probe order, so an `.ogg` beats a `.wav` and a `.jpg` beats a `.jpeg`.
inline constexpr std::string_view kChartFileExtension = ".aff";
inline constexpr std::array<std::string_view, 1> kChartFileExtensions = {".aff"};
inline constexpr std::array<std::string_view, 2> kAudioFileExtensions = {".ogg", ".wav"};
inline constexpr std::array<std::string_view, 3> kImageFileExtensions = {".jpg", ".jpeg", ".png"};

// Raw chart packages carry song metadata either as `songlist` (the name the
// game itself uses for its own asset) or as `songlist.json`. The extension-less
// spelling wins when a package ships both.
inline constexpr std::string_view kSonglistMetadataFile = "songlist";
inline constexpr std::string_view kSonglistMetadataJsonFile = "songlist.json";

// Logical AAsset path used by PST/PRS/FTR and by the custom Beyond hook
// that replaces sub_A74680's writable-dir {id}_{n} pack name.
inline std::string LocalChartAssetPath(std::string_view song_id, size_t difficulty) {
    std::string path;
    path.reserve(kSongsPrefix.size() + song_id.size() + 8);
    path.append(kSongsPrefix);
    path.append(song_id);
    // format_to appends straight into the reserved buffer, so the difficulty
    // never round-trips through a temporary std::string the way to_string would.
    std::format_to(std::back_inserter(path), "/{}.aff", difficulty);
    return path;
}
inline constexpr const char *kExtractedAudioStem = "/base";
inline constexpr const char *kExtractedJacketStem = "/jacket";
inline constexpr const char *kExtractedBackgroundStem = "/bg";
inline constexpr const char *kDefaultJacketAsset = "@official:img/default_jacket.jpg";
inline constexpr const char *kDefaultJacket256Asset = "@official:img/default_jacket_256.jpg";
inline constexpr const char *kSongSet = "base";
inline constexpr const char *kSongPurchase = "";
inline constexpr const char *kSongVersion = "4.0.0";
inline constexpr int kSongDate = 0;

// Runtime patch and hook signatures.
inline constexpr uint32_t kNopInstruction = 0xD503201F;
inline constexpr uint32_t kExpectedDigestSizeGuard = 0x540013A1;
inline constexpr uint32_t kExpectedDigestCompareGuard = 0x350012C0;

// IDA sub_E6F768 (7.0.0c): `return play->flags_0x110 & 1`. The retired
// April-Fools dynamix_conflict chart is the only setter of that byte, so
// timing/scenecontrol commands parsed from every other chart stay dormant
// (static lane geometry, no camera/track scheduling) in ~10 consumers,
// including play setup, touch->lane mapping and tap judgement. Swapping the
// getter for an always-true return arms those effects; with empty transform
// schedules the consumers reduce to vanilla behavior, so official charts are
// unaffected. Same gate family as the 4.x +0x108/264 fix documented by the
// custom-chart community.
inline constexpr std::array<uint8_t, 8> kExpectedScenecontrolGateGetter = {
    0x00, 0x40, 0x44, 0x39,  // LDRB W0, [X0, #0x110]
    0xC0, 0x03, 0x5F, 0xD6,  // RET
};
inline constexpr std::array<uint8_t, 8> kScenecontrolGateAlwaysTrue = {
    0x20, 0x00, 0x80, 0x52,  // MOV W0, #1
    0xC0, 0x03, 0x5F, 0xD6,  // RET
};
static_assert(kExpectedScenecontrolGateGetter.size() == kScenecontrolGateAlwaysTrue.size());

inline constexpr std::array<uint8_t, 16> kSigSonglistDifficultyFilter = {
    0xFF, 0x83, 0x03, 0xD1, 0xFD, 0x7B, 0x08, 0xA9,
    0xFC, 0x6F, 0x09, 0xA9, 0xFA, 0x67, 0x0A, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSigDifficultyAvailability = {
    0xFF, 0x83, 0x01, 0xD1, 0xFD, 0x7B, 0x02, 0xA9,
    0xF8, 0x5F, 0x03, 0xA9, 0xF6, 0x57, 0x04, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSigSongUnlockMaskCheck = {
    0xFF, 0xC3, 0x01, 0xD1, 0xFD, 0x7B, 0x04, 0xA9,
    0xF6, 0x57, 0x05, 0xA9, 0xF4, 0x4F, 0x06, 0xA9,
};

// IDA: sub_B87620 / sub_C987A8 treat a 1 from sub_121E9FC as "remote
// download present". Beyond (class 3) then selects img/download.png.
inline constexpr std::array<uint8_t, 16> kSigContentAvailability = {
    0xFD, 0x7B, 0xBD, 0xA9, 0xF6, 0x57, 0x01, 0xA9,
    0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x03, 0x00, 0x91,
};

// IDA: sub_C987A8. loc_C9940C (Download song?) if avail==1 and
// (difficulty==3 or song+0x1C0). song+0x1C0 also switches preview BGM
// to dl_{id}/base.ogg, so custom songs only raise it for this call.
inline constexpr std::array<uint8_t, 16> kSigPlayLauncher = {
    0xFD, 0x7B, 0xBA, 0xA9, 0xFC, 0x6F, 0x01, 0xA9,
    0xFA, 0x67, 0x02, 0xA9, 0xF8, 0x5F, 0x03, 0xA9,
};

// IDA: sub_A74680. PST/PRS/FTR build songs/{id}/{n}.aff; Beyond (class 3)
// or song+0x1C0 builds writablePath + {id}_{n} (a download pack, not AAsset).
inline constexpr std::array<uint8_t, 16> kSigChartPath = {
    0xFF, 0x83, 0x03, 0xD1, 0xFD, 0x7B, 0x0B, 0xA9,
    0xF6, 0x57, 0x0C, 0xA9, 0xF4, 0x4F, 0x0D, 0xA9,
};

inline constexpr std::array<uint8_t, 16> kSigFindSongById = {
    0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9,
    0xFD, 0x03, 0x00, 0x91, 0xF3, 0x03, 0x00, 0xAA,
};

inline constexpr std::array<uint8_t, 16> kSigFmodLoadBgm = {
    0xFF, 0x03, 0x04, 0xD1, 0xFD, 0x7B, 0x0A, 0xA9,
    0xFC, 0x6F, 0x0B, 0xA9, 0xFA, 0x67, 0x0C, 0xA9,
};

inline constexpr const char *kFmodProviderLibrary = "libfmodProvider.so";
inline constexpr const char *kFmodLoadBgmSymbol =
    "_ZN24AudioProviderFMODAndroid7loadBGMEPKci";

} // namespace arc_helper::cfg::custom_charts
