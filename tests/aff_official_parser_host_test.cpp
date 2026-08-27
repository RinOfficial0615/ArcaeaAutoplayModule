#include <cassert>
#include <string>

#include "manager/custom_chart/AffNormalizer.hpp"
#include "manager/custom_chart/AffOfficialParser.hpp"

namespace {

bool Contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

} // namespace

int main() {
    using arc_helper::aff::CheckOfficial;
    using arc_helper::aff::Normalize;

    {
        const auto check = CheckOfficial(
            "AudioOffset:0\n"
            "TimingPointDensityFactor:1.00\n"
            "-\n"
            "timing(0,0.00,4.00);\n"
            "timing(100,9999999.00,-4.00);\n"
            "(10,1);\n"
            "hold(20,40,2);\n"
            "hold(20,40,2.50);\n"
            "arc(0,10,0.00,1.00,s,0.00,0.00,0,none,true)[arctap(5)];\n"
            "arc(0,10,0.00,1.00,s,0.00,0.00,0,none,false,4.00);\n"
            "arc(20,30,0.50,0.50,s,1.00,1.00,0,none,designant);\n"
            "camera(0,0.00,0.00,0.00,0.00,0.00,0.00,l,100);\n"
            "camera(10,0.00,0.00,0.00,0.00,0.00,0.00,reset,1);\n"
            "scenecontrol(0,hidegroup,0.00,1);\n"
            "scenecontrol(20,trackhide);\n"
            "timinggroup(){\n"
            "(30,1);\n"
            "};\n"
            "timinggroup(noinput_angley15){\n"
            "(40,1);\n"
            "};\n");
        assert(check.ok);
    }

    {
        const auto check = CheckOfficial("AudioOffset:0\n-\narc(0,10,0,1,s,0,0,0,none,true);\n");
        assert(!check.ok);
        assert(Contains(check.error, "float"));
    }

    {
        // Official 7.0 charts (cataclysmcry/deinosphainein) exercise arc color
        // index 3 (gold guide), arbitrary chart-specific group labels and the
        // enwiden* scenecontrol verbs. The token grammar is unchanged from
        // 6.x: everything must validate without parser adjustments.
        const auto check = CheckOfficial(
            "AudioOffset:0\n"
            "-\n"
            "timing(0,180.00,3.00);\n"
            "scenecontrol(149052,enwidencamera,5053.00,1);\n"
            "scenecontrol(149052,enwidenlanes,5053.00,1);\n"
            "arc(135999,136999,0.00,0.00,s,1.00,1.00,3,none,true)[arctap(135999)];\n"
            "arc(135999,136499,1.00,0.00,so,1.00,1.00,3,none,true,2.00);\n"
            "timinggroup(){\n"
            "(97263,1);\n"
            "};\n"
            "timinggroup(tracecoleeee00){\n"
            "scenecontrol(1263,hidegroup,0.00,0);\n"
            "(97282,2);\n"
            "};\n");
        assert(check.ok);
    }

    {
        const auto check = CheckOfficial(
            "AudioOffset:0\n-\narc(0,10,0.00,1.00,s,0.00,0.00,0,none,true)[arctap(5,1.50)];\n");
        assert(!check.ok);
    }

    {
        const auto check = CheckOfficial("AudioOffset:0\n-\nscenecontrol(-10000,hidegroup,0,1);\n");
        assert(!check.ok);
    }

    {
        const auto check = CheckOfficial("AudioOffset:0\n-\nflick(0,1,0,0,0,0);\n");
        assert(!check.ok);
        assert(Contains(check.error, "flick"));
    }

    {
        const auto normalized = Normalize(
            "AudioOffset:0\n-\n"
            "scenecontrol(-10000,hidegroup,0,1);\n"
            "timinggroup(name=\"hide\",noinput,angley=1.50,anglex=-90.00){\n"
            "(40,1);\n"
            "};\n"
            "arc(0,10,0.00,1.00,s,0.00,0.00,0,,true,4.00)[arctap(5,1.50)];\n"
            "arc(20,30,0.00,1.00,s,0.00,0.00,0,metal.wav,false);\n");
        const auto check = CheckOfficial(normalized.text);
        assert(check.ok);
        assert(Contains(normalized.text, "timinggroup(noinput_angley15_anglex2700){"));
        assert(!Contains(normalized.text, "angley-"));
        assert(Contains(normalized.text, "[arctap(5)];"));
        assert(Contains(normalized.text, ",none,true,4.00)"));
        assert(Contains(normalized.text, ",none,false)"));
        assert(!Contains(normalized.text, "metal"));
    }

    {
        const auto normalized = Normalize(
            "AudioOffset:0\n-\n"
            "timing(93512,102.50,0.00);\n"
            "arc(973,974,0.50,0.50,s,1.00,1.00,0,,true)[arctap(973)];\n"
            "arc(878,878,0.50,0.50,s,0.00,0.00,0,arc_wav,true)[arctap(878)];\n");
        const auto check = CheckOfficial(normalized.text);
        assert(check.ok);
        assert(Contains(normalized.text, "timing(93512,102.50,4.00);"));
        assert(Contains(normalized.text, "[arctap(973)];"));
        assert(Contains(normalized.text, "[arctap(878)];"));
        assert(!Contains(normalized.text, "arc_wav"));
    }
    return 0;
}
