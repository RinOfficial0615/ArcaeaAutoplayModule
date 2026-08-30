#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/AutoplayConfig.h"
#include "config/CustomChartConfig.h"
#include "game/GameProfile.hpp"
#include "manager/network/NetworkHandler.hpp"
#include "manager/custom_chart/CustomChartGameplaySession.hpp"
#include "utils/Sha256.hpp"
#include "utils/ZipArchive.hpp"
#include "utils/memory/ByteScanner.hpp"

namespace {

bool LowHandler(arc_helper::network::HandlerArgs &) { return false; }
bool HighHandler(arc_helper::network::HandlerArgs &) { return false; }

} // namespace

int main(int argc, char **argv) {
    using namespace arc_helper;
    assert(crypto::Sha256Hex("abc", 3) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Known-answer vectors for the padding paths a short input cannot reach:
    // 55 is the longest message that still pads inside a single block, 56
    // forces the extra length block, 64/65 straddle a block boundary inside
    // Update, and 200 drives Update's multi-block loop. The digests come from
    // an independent implementation (Python hashlib); the "abc" and 56-byte
    // cases are the FIPS 180-4 vectors. These hashes become custom-chart cache
    // keys, where a silent change would load the wrong chart.
    {
        constexpr std::string_view kAlphabet = "abcdefghijklmnopqrstuvwxyz";
        const auto sha256_of = [](std::string_view data) {
            return crypto::Sha256Hex(data.data(), data.size());
        };
        const auto repeated = [kAlphabet](size_t length) {
            std::string data;
            while (data.size() < length) data.append(kAlphabet);
            data.resize(length);
            return data;
        };
        assert(sha256_of("") ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert(sha256_of(repeated(55)) ==
               "595615dbe4f0f407ae397d08b4c2cb870cb9b0e11937416f950c5160acf9c005");
        assert(sha256_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        assert(sha256_of(repeated(64)) ==
               "2fcd5a0d60e4c941381fcc4e00a4bf8be422c3ddfafb93c809e8d1e2bfffae8e");
        assert(sha256_of(repeated(65)) ==
               "1b3cd1877ab2f2f19f7be001722554f336cb799df0329de0bb4c118dc6abc06d");
        assert(sha256_of(repeated(200)) ==
               "8013a82140d916576e2cf550b27449a368abec66cc154a7d9f599019d33aa3d2");
    }
    // FindUniqueBytesInRanges is a pure scan over caller-supplied ranges, so a
    // host test can point it at an ordinary buffer -- every candidate position
    // inside the range is readable. These cover the three outcomes the address
    // resolver branches on: exactly one match (return it), none, and two or
    // more (abandon the scan, but still report the count).
    {
        alignas(8) std::array<uint8_t, 256> buffer{};
        const uintptr_t base = reinterpret_cast<uintptr_t>(buffer.data());
        constexpr std::array<uint8_t, 4> kSig = {0xDE, 0xAD, 0xBE, 0xEF};
        const auto place = [&](size_t offset) {
            std::memcpy(buffer.data() + offset, kSig.data(), kSig.size());
        };
        const auto scan = [&](std::span<const mem::MemRange> ranges, int &hits) {
            return mem::FindUniqueBytesInRanges(ranges, kSig.data(), kSig.size(), 4, &hits);
        };
        const std::array<mem::MemRange, 1> whole{
            mem::MemRange{base, base + buffer.size()},
        };

        int hits = -1;
        assert(scan(whole, hits) == 0 && hits == 0);          // nothing there yet
        place(64);
        assert(scan(whole, hits) == base + 64 && hits == 1);  // exactly one
        place(128);
        assert(scan(whole, hits) == 0 && hits == 2);          // second hit abandons

        // A second hit in a *separate* range still counts as a second hit.
        const std::array<mem::MemRange, 2> split{
            mem::MemRange{base, base + 96},
            mem::MemRange{base + 96, base + buffer.size()},
        };
        assert(scan(split, hits) == 0 && hits == 2);

        // Ranges too short to hold the signature, and inverted ones, are skipped
        // rather than crashing or wrapping around.
        std::memset(buffer.data(), 0, buffer.size());
        place(64);
        const std::array<mem::MemRange, 3> degenerate{
            mem::MemRange{base, base + kSig.size() - 1},
            mem::MemRange{base + 200, base + 100},
            mem::MemRange{base, base + buffer.size()},
        };
        assert(scan(degenerate, hits) == base + 64 && hits == 1);

        // Alignment: the probe only lands on multiples of 4, so a signature one
        // byte off that grid is invisible at align=4 but found at align=1.
        std::memset(buffer.data(), 0, buffer.size());
        place(65);
        assert(scan(whole, hits) == 0 && hits == 0);
        assert(mem::FindUniqueBytesInRanges(whole, kSig.data(), kSig.size(), 1, &hits) ==
                   base + 65 &&
               hits == 1);

        // An empty range list never reports a hit.
        assert(mem::FindUniqueBytesInRanges(std::span<const mem::MemRange>{}, kSig.data(),
                                            kSig.size(), 4, &hits) == 0 &&
               hits == 0);
    }

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
        assert(cfg::autoplay::ScoreStateApplyJudgementSignature(cfg::GameVersionId::k7001c)
                   .data() == cfg::autoplay::kSig_6162c_ScoreState_applyJudgement.data());
        assert(cfg::autoplay::ScoreStateApplyMissSignature(cfg::GameVersionId::k7001c).data() ==
               cfg::autoplay::kSig_6162c_ScoreState_applyMiss.data());
        // 7.0.x shifts only the note-family derived tail.
        assert(cfg::autoplay::ArcIsVoidOffset(cfg::GameVersionId::k6168c) == 0x9C);
        assert(cfg::autoplay::ArcIsVoidOffset(cfg::GameVersionId::k7000c) == 0xA4);
        assert(cfg::autoplay::ArcActiveNowOffset(cfg::GameVersionId::k7000c) == 0xD0);
        assert(cfg::autoplay::NoteRuntimeXOffset(cfg::GameVersionId::k7000c) == 0xD4);
        assert(cfg::autoplay::NoteRuntimeYOffset(cfg::GameVersionId::k7000c) == 0xD8);
        assert(cfg::autoplay::HoldHeadActivatedOffset(cfg::GameVersionId::k7000c) == 0xA8);
        assert(cfg::autoplay::ArcIsVoidOffset(cfg::GameVersionId::k7001c) == 0xA4);
        assert(cfg::autoplay::ArcActiveNowOffset(cfg::GameVersionId::k7001c) == 0xD0);
        assert(cfg::autoplay::NoteRuntimeXOffset(cfg::GameVersionId::k7001c) == 0xD4);
        assert(cfg::autoplay::NoteRuntimeYOffset(cfg::GameVersionId::k7001c) == 0xD8);
        assert(cfg::autoplay::HoldHeadActivatedOffset(cfg::GameVersionId::k7001c) == 0xA8);
        assert(cfg::FindGameProfileByVersionString("7.0.0c") != nullptr);
        const auto *profile_7000 = cfg::FindGameProfileByVersionString("7.0.0c");
        assert(profile_7000 != nullptr);
        assert(std::string_view(profile_7000->version_name) == "7.0.0c");
        assert(profile_7000->capabilities.autoplay && profile_7000->capabilities.network &&
               profile_7000->capabilities.custom_charts);
        assert(profile_7000->custom_charts.expected_songlist_loader_call != 0);
        const auto *profile_7001 = cfg::FindGameProfileByVersionString("7.0.1c");
        assert(profile_7001 != nullptr);
        assert(std::string_view(profile_7001->version_name) == "7.0.1c");
        assert(profile_7001->capabilities.autoplay && profile_7001->capabilities.network &&
               profile_7001->capabilities.custom_charts);
        assert(profile_7001->custom_charts.expected_songlist_loader_call == 0x9430485F);
        // Scenecontrol gate override ships for 7.0.x; older profiles
        // keep the zero offset and the install skips that patch.
        assert(profile_7000->custom_charts.scenecontrol_gate_getter == 0xE6F768);
        assert(profile_7001->custom_charts.scenecontrol_gate_getter == 0x11A57A8);
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
