#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arc_helper::aff {

struct Diagnostic {
    int line = 0;
    std::string item;
    std::string status;
    std::string detail;
};

// Sibling-file seam used to inline include()/fragment(). Tests pass a
// map; the importer passes ZIP lookups. Null means references are dropped.
class Source {
public:
    virtual ~Source() = default;
    virtual std::optional<std::string> ReadRelative(std::string_view from_file,
                                                    std::string_view relative) const = 0;
};

struct Result {
    std::string text;
    std::vector<Diagnostic> diagnostics;
};

// Rewrites ArcCreate AFF into the official token grammar (verified against
// official 6.x and 7.0 charts).
Result Normalize(std::string_view text,
                 std::string_view source_name = {},
                 const Source *files = nullptr);

} // namespace arc_helper::aff
