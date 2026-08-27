#include <cassert>
#include <string>
#include <unordered_map>

#include "manager/custom_chart/AffNormalizer.hpp"
#include "manager/custom_chart/AffOfficialParser.hpp"

namespace {

class MapSource final : public arc_helper::aff::Source {
public:
    std::unordered_map<std::string, std::string> files;

    std::optional<std::string> ReadRelative(std::string_view from_file,
                                            std::string_view relative) const override {
        std::string key(relative);
        auto it = files.find(key);
        if (it == files.end()) {
            const auto slash = from_file.find_last_of("/\\");
            if (slash != std::string_view::npos) {
                it = files.find(std::string(from_file.substr(0, slash + 1)) + key);
            }
        }
        if (it == files.end()) return std::nullopt;
        return it->second;
    }
};

bool HasStatus(const arc_helper::aff::Result &result, std::string_view status) {
    for (const auto &diag : result.diagnostics) {
        if (diag.status == status) return true;
    }
    return false;
}

bool Contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

} // namespace

int main() {
    using arc_helper::aff::Normalize;

    {
        const auto result = Normalize(
            "AudioOffset:0\n-\n"
            "timing(0,0.00,4.00);\n"
            "timing(100,9999999.00,99999.00);\n"
            "scenecontrol(-10000,hidegroup,0,1);\n"
            "scenecontrol(20,groupalpha,1000,0);\n"
            "scenecontrol(30,trackdisplay,1000,0);\n"
            "scenecontrol(40,redline,0,1);\n"
            "timinggroup(name=\"hide\",noinput,noclip,fadingholds){\n"
            "(40,1);\n"
            "};\n"
            "timinggroup(angley900){\n"
            "(50,1);\n"
            "};\n"
            "timinggroup(noinput_anglex1800){\n"
            "(60,1);\n"
            "};\n");
        assert(Contains(result.text, "timing(0,0.00,4.00);"));
        assert(Contains(result.text, "timing(100,9999999.00,99999.00);"));
        assert(Contains(result.text, "scenecontrol(0,hidegroup,0.00,1);"));
        assert(Contains(result.text, "scenecontrol(20,hidegroup,0.00,1);"));
        assert(Contains(result.text, "scenecontrol(30,trackdisplay,1000.00,0);"));
        assert(Contains(result.text, "scenecontrol(40,redline,0.00,1);"));
        assert(!Contains(result.text, "groupalpha"));
        assert(Contains(result.text, "timinggroup(noinput_noclip_fadingholds){"));
        assert(Contains(result.text, "timinggroup(angley900){"));
        assert(Contains(result.text, "timinggroup(noinput_anglex1800){"));
        assert(!Contains(result.text, "name="));
        assert(HasStatus(result, "DROPPED_COMMAND"));
        assert(HasStatus(result, "REWRITTEN"));
    }

    {
        const auto result = Normalize(
            "TimingPointsDensityFactor:1.5\n"
            "UnknownHeader:1\n"
            "-\n"
            "flick(0,1,0,0,0,0);\n"
            "camera(0,0.00,0.00,0.00,0.00,0.00,0.00,l,100);\n"
            "arc(0,10,0.00,1.00,s,0.00,0.00,0,none,true,4.00)[arctap(5,1.50)];\n"
            "timinggroup(noinput,angley=1.50,anglex=-90.00){\n"
            "(50,1);\n"
            "};\n");
        assert(Contains(result.text, "TimingPointDensityFactor:1.5"));
        assert(!Contains(result.text, "UnknownHeader"));
        assert(!Contains(result.text, "flick("));
        assert(Contains(result.text, "camera(0,0.00,0.00,0.00,0.00,0.00,0.00,l,100);"));
        assert(Contains(result.text, ",true,4.00)[arctap(5)];"));
        assert(!Contains(result.text, "arctap(5,1.50)"));
        assert(Contains(result.text, "timinggroup(noinput_angley15_anglex2700){"));
        assert(HasStatus(result, "DROPPED_HEADER"));
        assert(HasStatus(result, "DROPPED_COMMAND"));
        assert(HasStatus(result, "REWRITTEN"));
    }

    {
        MapSource source;
        source.files["extra.aff"] =
            "AudioOffset:0\n-\ntiming(0,120.00,4.00);\n(10,1);\nhold(20,40,2);\n";
        const auto result = Normalize(
            "AudioOffset:0\n-\ninclude(extra.aff);\nfragment(100,extra.aff);\n",
            "2.aff", &source);
        assert(Contains(result.text, "timing(0,120.00,4.00);"));
        assert(Contains(result.text, "(10,1);"));
        assert(Contains(result.text, "(110,1);"));
        assert(Contains(result.text, "hold(120,140,2);"));
        assert(HasStatus(result, "INLINED"));
    }

    {
        const auto result = Normalize(
            "AudioOffset:0\n-\ninclude(missing.aff);\nscenecontrol(0,foo,\"bar\",1);\n");
        assert(!Contains(result.text, "include"));
        assert(!Contains(result.text, "foo"));
        assert(HasStatus(result, "DROPPED_COMMAND"));
    }

    {
        const auto result = Normalize("timing(0,120,4);\n(0,1);\n");
        assert(Contains(result.text, "AudioOffset:0"));
        assert(Contains(result.text, "-"));
        assert(Contains(result.text, "timing(0,120.00,4.00);"));
        assert(HasStatus(result, "REWRITTEN"));
    }

    {
        const auto result = Normalize(
            "AudioOffset:0\n-\n"
            "timing(0,205.00,4.00);\n"
            "timing(93512,102.50,0.00);\n"
            "timing(1490,99999.00,0.00);\n");
        assert(Contains(result.text, "timing(93512,102.50,4.00);"));
        assert(Contains(result.text, "timing(1490,99999.00,4.00);"));
        assert(!Contains(result.text, ",0.00);"));
        assert(HasStatus(result, "REWRITTEN"));
    }

    {
        const auto result = Normalize(
            "AudioOffset:0\n-\n"
            "timing(0,185.00,4.00);\n"
            "arc(973,974,0.50,0.50,s,1.00,1.00,0,,true)[arctap(973)];\n"
            "arc(1622,1623,0.50,0.50,s,0.00,0.00,0,,true)[arctap(1622)];\n"
            "arc(878,878,0.50,0.50,sisi,1.00,1.00,0,none,true)[arctap(878)];\n"
            "arc(20,30,0.00,1.00,s,0.00,0.00,0,arc_wav,false);\n"
            "arc(40,50,0.00,1.00,s,0.00,0.00,0,metal.wav,false);\n");
        assert(Contains(result.text, "arc(973,975,0.50,0.50,s,1.00,1.00,0,none,true)[arctap(973)];"));
        assert(Contains(result.text, "arc(1622,1624,0.50,0.50,s,0.00,0.00,0,none,true)[arctap(1622)];"));
        assert(Contains(result.text, "arc(878,880,0.50,0.50,sisi,1.00,1.00,0,none,true)[arctap(878)];"));
        assert(Contains(result.text, ",none,false);"));
        assert(!Contains(result.text, "arc_wav"));
        assert(!Contains(result.text, "metal"));
        assert(HasStatus(result, "REWRITTEN"));
    }
    {
        // 7.0 official charts: gold guide arcs (arc color index 3), arbitrary
        // chart-specific timing-group labels and the enwiden* scenecontrol
        // verbs are all official tokens now and must survive verbatim.
        const auto result = Normalize(
            "AudioOffset:0\n-\n"
            "timing(0,180.00,3.00);\n"
            "scenecontrol(149052,enwidencamera,5053.00,1);\n"
            "scenecontrol(149052,enwidenlanes,5053.00,1);\n"
            "arc(135999,136999,0.00,0.00,s,1.00,1.00,3,none,true)[arctap(135999)];\n"
            "timinggroup(tracecoleeee00){\n"
            "(97263,1);\n"
            "};\n");
        assert(Contains(result.text, "scenecontrol(149052,enwidencamera,5053.00,1);"));
        assert(Contains(result.text, "scenecontrol(149052,enwidenlanes,5053.00,1);"));
        assert(Contains(result.text,
                        "arc(135999,136999,0.00,0.00,s,1.00,1.00,3,none,true)[arctap(135999)];"));
        assert(Contains(result.text, "timinggroup(tracecoleeee00){"));
        assert(arc_helper::aff::CheckOfficial(result.text).ok);
    }

    {
        // Labels reserved by the AFF lexer cannot be emitted as idents; they
        // stay dropped like the ArcCreate-only properties.
        const auto result = Normalize(
            "AudioOffset:0\n-\ntiminggroup(true){\n(0,1);\n};\n");
        assert(!Contains(result.text, "true"));
        assert(HasStatus(result, "DROPPED_COMMAND"));
    }
    return 0;
}
