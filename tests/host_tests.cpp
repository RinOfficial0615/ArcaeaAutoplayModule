#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/AutoplayConfig.h"
#include "config/CustomChartConfig.h"
#include "game/GameProfile.hpp"
#include "manager/network/NetworkHandler.hpp"
#include "manager/custom_chart/CustomChartGameplaySession.hpp"
#include "utils/Sha256.hpp"
#include "utils/ZipArchive.hpp"

namespace {

bool LowHandler(arc_helper::network::HandlerArgs &) { return false; }
bool HighHandler(arc_helper::network::HandlerArgs &) { return false; }

} // namespace

int main(int argc, char **argv) {
    using namespace arc_helper;
    {
        const auto parsed = nlohmann::json::parse(
            R"({"ok":true,"n":12.5,"s":"\u4f60\u597d","a":[null,false]})", nullptr, false);
        assert(!parsed.is_discarded());
        assert(parsed.at("ok").get<bool>());
        assert(parsed.at("s").get<std::string>() == "你好");
        assert(nlohmann::json::parse(R"({"x":tru})", nullptr, false).is_discarded());
    }
    assert(crypto::Sha256Hex("abc", 3) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(std::string_view(network::HttpMethodStr(0)) == "GET");
    assert(std::string_view(network::HttpMethodStr(3)) == "DELETE");
    assert(std::string_view(network::HttpMethodStr(99)) == "UNK");
    assert(network::HttpMethodBit(0) == cfg::network_block::kMethodGet);
    assert(network::HttpMethodBit(3) == cfg::network_block::kMethodDelete);
    assert(network::HttpMethodBit(99) == 0);

    {
        assert(cfg::FindGameProfileByVersionString(nullptr) == nullptr);
        assert(cfg::FindGameProfileByVersionString("") == nullptr);
        assert(cfg::FindGameProfileByVersionString("9.9.9") == nullptr);
        assert(cfg::FindGameProfileByVersionString("6.16.2") == nullptr);
        const auto *profile_616 = cfg::FindGameProfileByVersionString("6.16.2c");
        assert(profile_616 != nullptr);
        assert(std::string_view(profile_616->version_name) == "6.16.2c");
        assert(profile_616->capabilities.custom_charts);
        const auto *profile_6168 = cfg::FindGameProfileByVersionString("6.16.8c");
        assert(profile_6168 != nullptr);
        assert(std::string_view(profile_6168->version_name) == "6.16.8c");
        assert(profile_6168->capabilities.autoplay && profile_6168->capabilities.network &&
               profile_6168->capabilities.custom_charts);
        assert(profile_6168->custom_charts.expected_songlist_loader_call != 0);
        assert(cfg::autoplay::ScoreStateApplyJudgementSignature(cfg::GameVersionId::k6168c)
                   .data() == cfg::autoplay::kSig_6162c_ScoreState_applyJudgement.data());
        assert(cfg::autoplay::ScoreStateApplyMissSignature(cfg::GameVersionId::k6168c).data() ==
               cfg::autoplay::kSig_6162c_ScoreState_applyMiss.data());
        assert(cfg::autoplay::ScoreStateApplyJudgementSignature(cfg::GameVersionId::k7000c)
                   .data() == cfg::autoplay::kSig_6162c_ScoreState_applyJudgement.data());
        assert(cfg::autoplay::ScoreStateApplyMissSignature(cfg::GameVersionId::k7000c).data() ==
               cfg::autoplay::kSig_6162c_ScoreState_applyMiss.data());
        // 7.0.0c shifts only the note-family derived tail.
        assert(cfg::autoplay::ArcIsVoidOffset(cfg::GameVersionId::k6168c) == 0x9C);
        assert(cfg::autoplay::ArcIsVoidOffset(cfg::GameVersionId::k7000c) == 0xA4);
        assert(cfg::autoplay::ArcActiveNowOffset(cfg::GameVersionId::k7000c) == 0xD0);
        assert(cfg::autoplay::NoteRuntimeXOffset(cfg::GameVersionId::k7000c) == 0xD4);
        assert(cfg::autoplay::NoteRuntimeYOffset(cfg::GameVersionId::k7000c) == 0xD8);
        assert(cfg::autoplay::HoldHeadActivatedOffset(cfg::GameVersionId::k7000c) == 0xA8);
        assert(cfg::FindGameProfileByVersionString("7.0.0c") != nullptr);
        const auto *profile_7000 = cfg::FindGameProfileByVersionString("7.0.0c");
        assert(profile_7000 != nullptr);
        assert(std::string_view(profile_7000->version_name) == "7.0.0c");
        assert(profile_7000->capabilities.autoplay && profile_7000->capabilities.network &&
               profile_7000->capabilities.custom_charts);
        assert(profile_7000->custom_charts.expected_songlist_loader_call != 0);
        // Scenecontrol gate override ships only for 7.0.0c; older profiles
        // keep the zero offset and the install skips that patch.
        assert(profile_7000->custom_charts.scenecontrol_gate_getter == 0xE6F768);
        assert(profile_6168->custom_charts.scenecontrol_gate_getter == 0);
        assert(profile_616->custom_charts.scenecontrol_gate_getter == 0);
        assert(cfg::custom_charts::kExpectedScenecontrolGateGetter.size() ==
               cfg::custom_charts::kScenecontrolGateAlwaysTrue.size());
        assert(cfg::custom_charts::kExpectedScenecontrolGateGetter.front() == 0x00 &&
               cfg::custom_charts::kExpectedScenecontrolGateGetter[2] == 0x44);
        assert(cfg::FindGameProfileByVersionString("6.12.11c") != nullptr);
        assert(cfg::FindGameProfileByVersionString("6.13.2f") != nullptr);
        assert(cfg::FindGameProfileByVersionString("6.14.0c") != nullptr);
    }

    {
        auto &session = CustomChartGameplaySession::Instance();
        session.ResetForTesting();
        assert(!session.IsActive());
        session.OnAssetRead("assets/background/base.jpg");
        assert(!session.IsActive());
        assert(cfg::custom_charts::LocalChartAssetPath("ah_lostrequi_422e6e2f", 3) ==
               "songs/ah_lostrequi_422e6e2f/3.aff");
        assert(cfg::custom_charts::LocalChartAssetPath("ah_demo", 2) == "songs/ah_demo/2.aff");
        assert(cfg::custom_charts::kDifficultyCount == 5);
        // Official songlist side domain: 0..4 (side 4 first used in 7.0.0c).
        assert(cfg::custom_charts::kMaximumSide == 4);
        assert(cfg::custom_charts::kDifficultyPointersOffset == 0x228);
        assert(cfg::custom_charts::kDifficultyPresenceOffset == 0x250);
        assert(cfg::custom_charts::kDifficultyLockOffset == 0xF0);
        assert(cfg::custom_charts::kDifficultyObjectReadableBytes == 0x128);
        assert(cfg::custom_charts::kSongRegistryOwnerRegistryOffset == 32);
        session.OnCustomChartMapped("songs/ah_demo/3.aff");
        const auto started = session.Read();
        assert(started.active);
        session.OnAssetRead("songs/ah_demo/jacket.jpg");
        assert(session.IsActive());
        session.OnAssetRead("assets/background/base.png");
        assert(session.IsActive());
        session.OnAssetRead("assets/background/suffixbase.jpg");
        assert(!session.IsActive());
        assert(session.Read().generation == started.generation);
        session.OnCustomChartMapped("file:///android_asset/songs/ah_demo/3.aff");
        assert(session.IsActive());
        assert(session.Read().generation != started.generation);
        session.OnAssetRead("songs/ah_demo/base.jpg");
        assert(!session.IsActive());
    }

    {
        const auto empty = network::HandlerSnapshot::Empty();
        const auto low = empty->With({"low", 10, 0, LowHandler});
        const auto ordered = low->With({"high", 20, 1, HighHandler});
        assert(empty->Entries().empty());
        assert(low->Entries().size() == 1);
        assert(ordered->Entries().size() == 2);
        assert(ordered->Entries()[0].name == "high");
        assert(ordered->Entries()[1].name == "low");
        assert(ordered->Contains("high", nullptr) == true);

        network::BufferView view{};
        view.data = reinterpret_cast<const uint8_t *>("payload");
        view.full_len = 7;
        view.show_len = 7;
        view.status = network::BufferViewStatus::Ok;
        const auto limited = view.Limit(3);
        assert(limited.show_len == 3);
        assert(limited.full_len == 7);
        assert(limited.Truncated());
    }

    for (int i = 1; i < argc; ++i) {
        bool expect_fail = false;
        std::string path = argv[i];
        if (path == "--expect-fail" && i + 1 < argc) {
            expect_fail = true;
            path = argv[++i];
        }
        zip::Archive archive;
        std::string error;
        const bool opened = archive.Open(path, error);
        if (expect_fail) {
            if (opened) {
                std::cerr << "unsafe archive unexpectedly opened: " << path << '\n';
                return 2;
            }
            continue;
        }
        if (!opened) {
            std::cerr << "archive open failed: " << path << ": " << error << '\n';
            return 3;
        }
        size_t files = 0;
        for (const auto &entry : archive.Entries()) {
            if (entry.directory) continue;
            ++files;
            if (entry.uncompressed_size <= 2 * 1024 * 1024) {
                std::vector<uint8_t> data;
                if (!archive.Extract(entry, data, error)) {
                    std::cerr << "extract failed: " << entry.name << ": " << error << '\n';
                    return 4;
                }
            }
        }
        if (!files) return 5;
    }
    std::cout << "host tests passed\n";
    return 0;
}
