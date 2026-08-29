#include "manager/custom_chart/AffNormalizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "config/CustomChartConfig.h"

namespace arc_helper::aff {
namespace {

constexpr std::array<std::string_view, 9> kOfficialScenecontrol = {
    "hidegroup", "trackhide", "trackshow", "trackdisplay", "redline",
    "enwidencamera", "enwidenlanes", "arcahvdistort", "arcahvdebris",
};

constexpr std::string_view kBlanks = " \t\r";

// Borrows `value`; callers that need an owned copy use Trim() below.
std::string_view TrimView(std::string_view value) {
    const size_t begin = value.find_first_not_of(kBlanks);
    if (begin == std::string_view::npos) return {};
    return value.substr(begin, value.find_last_not_of(kBlanks) - begin + 1);
}

std::string Trim(std::string_view value) { return std::string(TrimView(value)); }

// Splits on '\n' and strips the '\r' of a CRLF pair -- packages authored on
// Windows ship those. The produced views borrow `text`, so consume them within
// the same statement/loop and never store them.
constexpr auto LinesOf(std::string_view text) {
    return std::views::split(text, '\n') | std::views::transform([](auto &&line) {
        std::string_view view(line.begin(), line.end());
        if (view.ends_with('\r')) view.remove_suffix(1);
        return view;
    });
}

std::string Lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string StripSemicolon(std::string line) {
    if (!line.empty() && line.back() == ';') line.pop_back();
    return line;
}

std::vector<std::string> SplitTopLevel(std::string_view inner) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    bool in_string = false;
    char quote = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (in_string) {
            current.push_back(c);
            if (c == '\\' && i + 1 < inner.size()) {
                current.push_back(inner[++i]);
                continue;
            }
            if (c == quote) in_string = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            current.push_back(c);
            continue;
        }
        if (c == '(' || c == '[') {
            ++depth;
            current.push_back(c);
            continue;
        }
        if (c == ')' || c == ']') {
            if (depth) --depth;
            current.push_back(c);
            continue;
        }
        if (c == ',' && depth == 0) {
            parts.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    const std::string last = Trim(current);
    if (!last.empty() || !parts.empty()) parts.push_back(last);
    return parts;
}

struct Call {
    std::string name;
    std::vector<std::string> args;
    std::string suffix; // e.g. [arctap(...)...]
};

std::optional<Call> ParseCall(std::string_view line) {
    const std::string trimmed = StripSemicolon(Trim(line));
    if (trimmed.empty()) return std::nullopt;
    size_t open = trimmed.find('(');
    if (open == std::string::npos) return std::nullopt;
    Call call;
    call.name = Trim(trimmed.substr(0, open));
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < trimmed.size(); ++i) {
        if (trimmed[i] == '(') ++depth;
        else if (trimmed[i] == ')') {
            --depth;
            if (depth == 0) {
                close = i;
                break;
            }
        }
    }
    if (close == std::string::npos) return std::nullopt;
    call.args = SplitTopLevel(std::string_view(trimmed).substr(open + 1, close - open - 1));
    call.suffix = trimmed.substr(close + 1);
    return call;
}

bool ParseDouble(std::string_view text, double &out) {
    const std::string token = Trim(text);
    if (token.empty()) return false;
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(token.c_str(), &end);
    if (errno == ERANGE || !end || end != token.c_str() + token.size() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool ParseInt(std::string_view text, int64_t &out) {
    const std::string token = Trim(text);
    if (token.empty()) return false;
    char *end = nullptr;
    errno = 0;
    const long long value = std::strtoll(token.c_str(), &end, 10);
    if (errno == ERANGE || !end || end != token.c_str() + token.size()) return false;
    out = static_cast<int64_t>(value);
    return true;
}

bool LooksLikeInteger(std::string_view text) {
    const std::string_view token = TrimView(text);
    if (token.empty()) return false;
    const std::string_view digits =
        (token.front() == '+' || token.front() == '-') ? token.substr(1) : token;
    if (digits.empty()) return false;
    return std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
}

bool HasQuote(std::string_view text) {
    return text.contains('"') || text.contains('\'');
}

std::string FormatFloat(double value) {
    return std::format("{:.2f}", value);
}

bool OfficialScenecontrol(std::string_view type) {
    const std::string key = Lower(std::string(type));
    return std::ranges::contains(kOfficialScenecontrol, std::string_view(key));
}

void AddDiag(std::vector<Diagnostic> &out, int line, std::string_view item,
             std::string_view status, std::string_view detail) {
    out.push_back({line, std::string(item), std::string(status), std::string(detail)});
}

std::string JoinArgs(const std::vector<std::string> &args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out.push_back(',');
        out += args[i];
    }
    return out;
}

std::string EmitCall(const Call &call) {
    std::string out = call.name;
    out.push_back('(');
    out += JoinArgs(call.args);
    out.push_back(')');
    out += call.suffix;
    if (out.empty() || out.back() != ';') out.push_back(';');
    return out;
}

std::string Unquote(std::string_view value) {
    std::string text = Trim(value);
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

std::string DirName(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos) return {};
    return std::string(path.substr(0, slash + 1));
}

std::string JoinRelative(std::string_view from_file, std::string_view relative) {
    std::string path = DirName(from_file);
    path += Trim(relative);
    std::string normalized;
    normalized.reserve(path.size());
    for (char c : path) normalized.push_back(c == '\\' ? '/' : c);
    return normalized;
}

// AFF timings are non-negative and must stay within the official int32
// domain: nested fragment shifts may not re-emit negative or oversized times.
int64_t AddTiming(int64_t value, int64_t offset) {
    value = std::clamp(value, static_cast<int64_t>(0), static_cast<int64_t>(INT32_MAX));
    offset = std::clamp(offset, static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX));
    const int64_t shifted = value + offset;
    return shifted < 0 ? 0 : std::min(shifted, static_cast<int64_t>(INT32_MAX));
}

std::string ShiftNumber(const std::string &token, int64_t offset) {
    int64_t value = 0;
    if (!ParseInt(token, value)) return token;
    return std::to_string(AddTiming(value, offset));
}

// Rewrites every `arctap(...)` argument list inside `suffix` and copies the
// surrounding text verbatim. An unterminated `arctap(` is copied as-is rather
// than dropped -- deciding what to report stays with the caller's diagnostics.
// `rewrite` takes the split arguments by value and returns the replacement list.
template <typename Rewrite>
std::string RewriteArcTapSuffix(std::string_view suffix, Rewrite &&rewrite) {
    constexpr std::string_view kArcTap = "arctap(";
    std::string out;
    out.reserve(suffix.size());
    for (size_t i = 0; i < suffix.size();) {
        const size_t open = suffix.find(kArcTap, i);
        if (open == std::string_view::npos) {
            out.append(suffix.substr(i));
            break;
        }
        out.append(suffix.substr(i, open - i));
        const size_t args_begin = open + kArcTap.size();
        const size_t close = suffix.find(')', args_begin);
        if (close == std::string_view::npos) {
            out.append(suffix.substr(open));
            break;
        }
        out += kArcTap;
        out += JoinArgs(rewrite(SplitTopLevel(suffix.substr(args_begin, close - args_begin))));
        out.push_back(')');
        i = close + 1;
    }
    return out;
}

std::string ShiftArcTaps(std::string_view suffix, int64_t offset) {
    return RewriteArcTapSuffix(suffix, [offset](std::vector<std::string> args) {
        if (!args.empty()) args[0] = ShiftNumber(args[0], offset);
        return args;
    });
}

std::string ShiftLine(std::string_view line, int64_t offset) {
    auto call = ParseCall(line);
    if (!call || call->args.empty()) return std::string(line);
    const std::string name = Lower(call->name);
    const bool timing = name == "timing";
    int64_t first = 0;
    const bool first_int = ParseInt(call->args[0], first);
    if (timing && first_int && first == 0) return std::string(Trim(line).back() == ';' ? line : std::string(line) + ";");
    if (first_int) call->args[0] = std::to_string(AddTiming(first, offset));
    if ((name == "hold" || name == "arc") && call->args.size() >= 2) {
        call->args[1] = ShiftNumber(call->args[1], offset);
    }
    if (name == "arc" && !call->suffix.empty()) call->suffix = ShiftArcTaps(call->suffix, offset);
    return EmitCall(*call);
}

struct NormalizeState {
    const Source *files = nullptr;
    std::set<std::string> active;
    std::vector<Diagnostic> *diagnostics = nullptr;
    int depth = 0;
};

std::string BodyAfterHeader(std::string_view text) {
    bool header = true;
    std::string body;
    for (const std::string_view line : LinesOf(text)) {
        const std::string_view trimmed = TrimView(line);
        if (header) {
            if (trimmed == "-") header = false;
            continue;
        }
        if (!body.empty()) body.push_back('\n');
        body += trimmed;
    }
    return body;
}

// The official grammar takes a single identifier label after `timinggroup(`
// and treats unrecognized labels as inert: the engine only matches
// noinput/fadingholds and anglex<tenths>/angley<tenths> segments, so official
// charts ship arbitrary chart-specific names (7.0 uses e.g.
// tracecoleeee00). Labels stay verbatim instead of being dropped.
bool TimingGroupLabel(std::string_view token) {
    if (token.empty()) return false;
    const auto ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    if (!std::isalpha(static_cast<unsigned char>(token.front())) && token.front() != '_') {
        return false;
    }
    return std::ranges::all_of(token, ident_char);
}

// Tokens reserved by the official lexer; a group label spelling one of these
// would lex as a keyword, not an identifier, in both the game parser and
// CheckOfficial.
bool TimingGroupLabelKeyword(std::string_view token) {
    constexpr std::array<std::string_view, 13> kKeywords = {
        "timing",   "hold",   "arc",      "camera",   "scenecontrol",
        "timinggroup", "flick", "arctap",  "at",       "true",
        "false",    "rgb",    "designant",
    };
    return std::ranges::contains(kKeywords, token);
}

std::string ClampIntTiming(const std::string &token, int line, std::string_view item,
                           std::vector<Diagnostic> &diagnostics) {
    int64_t value = 0;
    if (!ParseInt(token, value) || value >= 0) return token;
    AddDiag(diagnostics, line, std::string(item), "REWRITTEN",
            "negative integer timing clamped to 0");
    return "0";
}

std::string RewriteTiming(Call call, int line, std::vector<Diagnostic> &diagnostics) {
    if (!call.args.empty()) {
        call.args[0] = ClampIntTiming(call.args[0], line, "timing", diagnostics);
    }
    if (call.args.size() < 3) return EmitCall(call);
    double bpm = 0, divisor = 0;
    if (!ParseDouble(call.args[1], bpm) || !ParseDouble(call.args[2], divisor)) {
        return EmitCall(call);
    }
    const bool was_int_bpm = LooksLikeInteger(call.args[1]);
    const bool was_int_divisor = LooksLikeInteger(call.args[2]);
    // LogicChart::setupTimingBars steps by (60000/bpm)*beats. A zero
    // beats value never advances the cursor and hangs chart load.
    if (std::fabs(divisor) < 1e-12) {
        divisor = 4.0;
        AddDiag(diagnostics, line, "timing", "REWRITTEN",
                "zero timing beats clamped to 4.00");
    }
    call.args[1] = FormatFloat(bpm);
    call.args[2] = FormatFloat(divisor);
    if (was_int_bpm || was_int_divisor) {
        AddDiag(diagnostics, line, "timing", "REWRITTEN", "timing float arguments");
    }
    return EmitCall(call);
}

std::optional<std::string> RewriteScenecontrol(Call call, int line,
                                               std::vector<Diagnostic> &diagnostics) {
    if (call.args.size() < 2) {
        AddDiag(diagnostics, line, "scenecontrol", "DROPPED_COMMAND", "missing type");
        return std::nullopt;
    }
    call.args[0] = ClampIntTiming(call.args[0], line, "scenecontrol", diagnostics);
    const std::string type = Lower(call.args[1]);
    if (type == "groupalpha") {
        double alpha = 255;
        if (call.args.size() >= 4) ParseDouble(call.args[3], alpha);
        else if (call.args.size() >= 3) ParseDouble(call.args[2], alpha);
        call.args = {call.args[0], "hidegroup", "0.00", alpha == 0.0 ? "1" : "0"};
        AddDiag(diagnostics, line, "groupalpha", "REWRITTEN",
                "groupalpha mapped to hidegroup");
        return EmitCall(call);
    }
    if (!OfficialScenecontrol(type)) {
        AddDiag(diagnostics, line, type, "DROPPED_COMMAND",
                "ArcCreate-only scenecontrol");
        return std::nullopt;
    }
    call.args[1] = type;
    std::vector<std::string> extra(call.args.begin() + 2, call.args.end());
    if (std::ranges::any_of(extra, HasQuote)) {
        AddDiag(diagnostics, line, type, "DROPPED_COMMAND", "string scenecontrol argument");
        return std::nullopt;
    }
    if (extra.size() > 2) {
        AddDiag(diagnostics, line, type, "REWRITTEN", "extra scenecontrol arguments dropped");
        extra.resize(2);
    }
    if (extra.size() == 1) extra.push_back("0");
    if (extra.size() >= 2) {
        double first = 0;
        int64_t second = 0;
        if (!ParseDouble(extra[0], first)) {
            AddDiag(diagnostics, line, type, "DROPPED_COMMAND", "invalid float argument");
            return std::nullopt;
        }
        if (LooksLikeInteger(extra[1])) {
            if (!ParseInt(extra[1], second)) {
                AddDiag(diagnostics, line, type, "DROPPED_COMMAND", "invalid int argument");
                return std::nullopt;
            }
        } else {
            double as_float = 0;
            if (!ParseDouble(extra[1], as_float)) {
                AddDiag(diagnostics, line, type, "DROPPED_COMMAND", "invalid int argument");
                return std::nullopt;
            }
            second = static_cast<int64_t>(as_float);
        }
        if (LooksLikeInteger(extra[0])) {
            AddDiag(diagnostics, line, type, "REWRITTEN", "scenecontrol float argument");
        }
        extra[0] = FormatFloat(first);
        extra[1] = std::to_string(second);
    }
    call.args.resize(2);
    call.args.insert(call.args.end(), extra.begin(), extra.end());
    return EmitCall(call);
}

std::optional<std::string> OfficialAngleFromProperty(std::string_view prop) {
    const bool x = prop.starts_with("anglex=");
    const bool y = prop.starts_with("angley=");
    if (!x && !y) return std::nullopt;
    double degrees = 0;
    if (!ParseDouble(prop.substr(7), degrees)) return std::string{};
    int64_t tenths = static_cast<int64_t>(std::llround(degrees * 10.0));
    tenths %= 3600;
    if (tenths < 0) tenths += 3600;
    if (tenths == 0) return std::string{};
    return std::format("{}{}", x ? "anglex" : "angley", tenths);
}

std::string RewriteTimingGroup(Call call, int line, std::vector<Diagnostic> &diagnostics) {
    std::vector<std::string> kept;
    for (const auto &raw : call.args) {
        if (raw.empty()) continue;
        const std::string prop = Lower(Unquote(raw));
        if (const auto angle = OfficialAngleFromProperty(prop)) {
            if (angle->empty()) {
                AddDiag(diagnostics, line, raw, "DROPPED_COMMAND",
                        "ArcCreate timinggroup property");
                continue;
            }
            if (!std::ranges::contains(kept, *angle)) {
                kept.push_back(*angle);
                AddDiag(diagnostics, line, raw, "REWRITTEN", *angle);
            }
            continue;
        }
        if (prop.contains('=')) {
            AddDiag(diagnostics, line, raw, "DROPPED_COMMAND",
                    "ArcCreate timinggroup property");
            continue;
        }
        if (!TimingGroupLabel(prop) || TimingGroupLabelKeyword(prop)) {
            AddDiag(diagnostics, line, raw, "DROPPED_COMMAND",
                    "ArcCreate timinggroup property");
            continue;
        }
        // A label containing '_' is the final composed ident; single segments
        // join with '_'.
        if (prop.contains('_')) {
            kept.clear();
            kept.push_back(prop);
            break;
        }
        if (!std::ranges::contains(kept, prop)) kept.push_back(prop);
    }
    const std::string ident = std::ranges::fold_left(
        kept, std::string{}, [](std::string joined, const std::string &part) {
            if (!joined.empty()) joined.push_back('_');
            joined += part;
            return joined;
        });
    if (kept.size() > 1) {
        AddDiag(diagnostics, line, ident, "REWRITTEN", "timinggroup properties joined with _");
    }
    return std::format("timinggroup({}){{", ident);
}

bool IsTraceFlag(std::string_view raw) {
    const std::string text = Lower(Unquote(raw));
    return text == "true" || text == "false" || text == "designant";
}

bool SuffixHasArcTap(std::string_view suffix) { return suffix.contains("arctap("); }

std::string EnsureFloatArg(std::string token, int line, std::string_view item,
                           std::vector<Diagnostic> &diagnostics) {
    double value = 0;
    if (!ParseDouble(token, value)) return token;
    if (!LooksLikeInteger(token)) return token;
    AddDiag(diagnostics, line, std::string(item), "REWRITTEN", "float argument");
    return FormatFloat(value);
}

std::string EnsureIntArg(std::string token, int line, std::string_view item,
                         std::vector<Diagnostic> &diagnostics) {
    if (LooksLikeInteger(token)) return token;
    double value = 0;
    if (!ParseDouble(token, value)) return token;
    AddDiag(diagnostics, line, std::string(item), "REWRITTEN", "int argument");
    return std::to_string(static_cast<int64_t>(value));
}

std::string RewriteTimedEvent(Call call, int line, std::vector<Diagnostic> &diagnostics) {
    const std::string name = Lower(call.name);
    if (!call.args.empty()) {
        call.args[0] = ClampIntTiming(call.args[0], line, name.empty() ? "tap" : name,
                                      diagnostics);
    }
    if ((name == "hold" || name == "arc") && call.args.size() >= 2) {
        call.args[1] = ClampIntTiming(call.args[1], line, name, diagnostics);
    }
    if (name == "arc") {
        if (call.args.size() >= 7) {
            call.args[2] = EnsureFloatArg(call.args[2], line, "arc", diagnostics);
            call.args[3] = EnsureFloatArg(call.args[3], line, "arc", diagnostics);
            call.args[5] = EnsureFloatArg(call.args[5], line, "arc", diagnostics);
            call.args[6] = EnsureFloatArg(call.args[6], line, "arc", diagnostics);
        }
        // Empty sfx (`0,,true`) can leave the istrace flag in the sfx slot.
        if (call.args.size() >= 9 && IsTraceFlag(call.args[8])) {
            call.args.insert(call.args.begin() + 8, "none");
            AddDiag(diagnostics, line, "sfx", "REWRITTEN",
                    "inserted missing arc sfx identifier");
        }
        if (call.args.size() >= 9 && call.args[8] != "none") {
            // Custom hitsounds (`metal.wav`, `arc_wav`) are not in the APK
            // audio table; FMOD throws AudioProvider_error while loading.
            AddDiag(diagnostics, line, call.args[8].empty() ? "sfx" : call.args[8],
                    "REWRITTEN", "official arc sfx identifier");
            call.args[8] = "none";
        }
        const bool has_arctap = SuffixHasArcTap(call.suffix);
        if (has_arctap) {
            if (call.args.size() < 10) {
                while (call.args.size() < 9) call.args.push_back("none");
                call.args.push_back("true");
                AddDiag(diagnostics, line, "arc", "REWRITTEN",
                        "arctap carrier missing istrace");
            } else {
                const std::string flag = Lower(call.args[9]);
                if (flag != "true" && flag != "designant") {
                    AddDiag(diagnostics, line, "arc", "REWRITTEN",
                            "arctap carrier forced to trace");
                    call.args[9] = "true";
                }
            }
            int64_t t1 = 0, t2 = 0;
            const bool have_t = call.args.size() >= 2 && ParseInt(call.args[0], t1) &&
                                ParseInt(call.args[1], t2);
            if (have_t && t2 <= t1) {
                // setupArc interpolates arctaps by (t-t1)/(t2-t1); zero
                // duration yields NaN positions so the notes cannot be hit.
                AddDiag(diagnostics, line, "arc", "REWRITTEN",
                        "zero-length arctap carrier extended");
                t2 = t1 + 1;
                call.args[1] = std::to_string(t2);
            }
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            const bool same_point = have_t && call.args.size() >= 7 &&
                                    ParseDouble(call.args[2], x1) &&
                                    ParseDouble(call.args[3], x2) &&
                                    ParseDouble(call.args[5], y1) &&
                                    ParseDouble(call.args[6], y2) &&
                                    x1 == x2 && y1 == y2;
            // RenderArcNote::init skips mesh generation for 1ms same-point
            // traces. Official 1ms arctap ticks always change X or Y.
            if (same_point && t2 - t1 <= 1) {
                AddDiag(diagnostics, line, "arc", "REWRITTEN",
                        "same-point arctap carrier duration extended");
                call.args[1] = std::to_string(t1 + 2);
            }
        }
        if (call.args.size() > 11) {
            AddDiag(diagnostics, line, "arc", "REWRITTEN", "extra arc arguments dropped");
            call.args.resize(11);
        }
        if (call.args.size() == 11) {
            call.args[10] = EnsureFloatArg(call.args[10], line, "arc", diagnostics);
        }
    }
    if (name == "camera") {
        for (size_t i = 1; i <= 6 && i < call.args.size(); ++i) {
            call.args[i] = EnsureFloatArg(call.args[i], line, "camera", diagnostics);
        }
        if (call.args.size() >= 9) {
            call.args[8] = EnsureIntArg(call.args[8], line, "camera", diagnostics);
        }
    }
    if (name == "arc" && !call.suffix.empty()) {
        call.suffix = RewriteArcTapSuffix(call.suffix, [&](std::vector<std::string> args) {
            if (!args.empty()) args[0] = ClampIntTiming(args[0], line, "arctap", diagnostics);
            if (args.size() > 1) {
                AddDiag(diagnostics, line, "arctap", "REWRITTEN",
                        "official arctap accepts timing only");
                args.resize(1);
            }
            return args;
        });
    }
    return EmitCall(call);
}

std::string NormalizeDocument(std::string_view text, std::string_view source_name,
                              NormalizeState &state);

std::optional<std::string> InlineReference(std::string_view command, std::string_view path_arg,
                                           std::string_view from_file, int64_t offset, int line,
                                           NormalizeState &state) {
    if (!state.files) {
        AddDiag(*state.diagnostics, line, command, "DROPPED_COMMAND",
                "include/fragment requires package files");
        return std::nullopt;
    }
    if (state.depth >= cfg::custom_charts::kMaxAffIncludeDepth) {
        AddDiag(*state.diagnostics, line, path_arg, "DROPPED_COMMAND", "include depth limit");
        return std::nullopt;
    }
    const std::string relative = Unquote(path_arg);
    const std::string resolved = JoinRelative(from_file, relative);
    if (state.active.contains(resolved)) {
        AddDiag(*state.diagnostics, line, resolved, "DROPPED_COMMAND", "include cycle");
        return std::nullopt;
    }
    const auto content = state.files->ReadRelative(from_file, relative);
    if (!content) {
        AddDiag(*state.diagnostics, line, relative, "DROPPED_COMMAND", "include target missing");
        return std::nullopt;
    }
    state.active.insert(resolved);
    ++state.depth;
    const std::string nested = NormalizeDocument(*content, resolved, state);
    --state.depth;
    state.active.erase(resolved);
    std::string body = BodyAfterHeader(nested);
    if (offset != 0) {
        std::string shifted;
        for (const std::string_view line_text : LinesOf(body)) {
            if (!shifted.empty()) shifted.push_back('\n');
            shifted += ShiftLine(line_text, offset);
        }
        body = std::move(shifted);
    }
    AddDiag(*state.diagnostics, line, relative, "INLINED", command);
    return body;
}

std::string NormalizeDocument(std::string_view text, std::string_view source_name,
                              NormalizeState &state) {
    std::string buffer(text);
    if (buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF &&
        static_cast<unsigned char>(buffer[1]) == 0xBB &&
        static_cast<unsigned char>(buffer[2]) == 0xBF) {
        buffer.erase(0, 3);
    }
    std::string out;
    bool header = true;
    int line_number = 0;
    int open_groups = 0;
    auto append = [&](std::string_view text_line) {
        if (!out.empty()) out.push_back('\n');
        out += text_line;
    };
    for (const std::string_view line : LinesOf(buffer)) {
        ++line_number;
        const std::string_view trimmed = TrimView(line);
        if (trimmed.empty()) continue;
        if (header) {
            if (trimmed == "-") {
                append("-");
                header = false;
                continue;
            }
            const bool looks_like_event = trimmed.contains('(') || trimmed.starts_with("};");
            if (looks_like_event) {
                append("AudioOffset:0");
                append("-");
                header = false;
                AddDiag(*state.diagnostics, line_number, source_name, "REWRITTEN",
                        "inserted missing AFF header terminator");
            } else {
                const size_t colon = trimmed.find(':');
                if (colon == std::string_view::npos) {
                    AddDiag(*state.diagnostics, line_number, trimmed, "DROPPED_HEADER",
                            "invalid header line");
                    continue;
                }
                const std::string_view key = TrimView(trimmed.substr(0, colon));
                const std::string_view value = TrimView(trimmed.substr(colon + 1));
                // Both density spellings exist in the wild; the official key wins.
                const bool density = key == "TimingPointDensityFactor" ||
                                     key == "TimingPointsDensityFactor";
                const bool audio_offset = key == "AudioOffset";
                if (!density && !audio_offset) {
                    AddDiag(*state.diagnostics, line_number, key, "DROPPED_HEADER",
                            "unknown AFF header");
                    continue;
                }
                double header_number = 0;
                if (!ParseDouble(value, header_number)) {
                    AddDiag(*state.diagnostics, line_number, key, "DROPPED_HEADER",
                            audio_offset ? "invalid AudioOffset value"
                                         : "invalid density factor value");
                    continue;
                }
                if (key == "TimingPointsDensityFactor") {
                    AddDiag(*state.diagnostics, line_number, key, "REWRITTEN",
                            "TimingPointDensityFactor");
                }
                append(std::format("{}:{}", density ? "TimingPointDensityFactor" : key, value));
                continue;
            }
        }
        if (trimmed.front() == '#') {
            AddDiag(*state.diagnostics, line_number, trimmed, "DROPPED_COMMAND", "comment");
            continue;
        }
        if (trimmed.starts_with("};")) {
            if (open_groups > 0) --open_groups;
            append("};");
            continue;
        }
        auto call = ParseCall(trimmed);
        if (!call) {
            AddDiag(*state.diagnostics, line_number, trimmed, "DROPPED_COMMAND",
                    "unparsable AFF line");
            continue;
        }
        const std::string name = Lower(call->name);
        if (name == "timinggroup") {
            ++open_groups;
            append(RewriteTimingGroup(*call, line_number, *state.diagnostics));
            continue;
        }
        if (name == "timing") {
            append(RewriteTiming(*call, line_number, *state.diagnostics));
            continue;
        }
        if (name == "scenecontrol") {
            if (const auto rewritten = RewriteScenecontrol(*call, line_number, *state.diagnostics)) {
                append(*rewritten);
            }
            continue;
        }
        if (name == "include") {
            if (call->args.empty()) {
                AddDiag(*state.diagnostics, line_number, "include", "DROPPED_COMMAND",
                        "missing path");
                continue;
            }
            if (const auto body = InlineReference("include", call->args[0], source_name, 0,
                                                  line_number, state)) {
                if (!body->empty()) append(*body);
            }
            continue;
        }
        if (name == "fragment") {
            if (call->args.size() < 2) {
                AddDiag(*state.diagnostics, line_number, "fragment", "DROPPED_COMMAND",
                        "missing path");
                continue;
            }
            int64_t offset = 0;
            ParseInt(call->args[0], offset);
            if (const auto body = InlineReference("fragment", call->args[1], source_name, offset,
                                                  line_number, state)) {
                if (!body->empty()) append(*body);
            }
            continue;
        }
        if (name == "flick") {
            AddDiag(*state.diagnostics, line_number, "flick", "DROPPED_COMMAND",
                    "official parseNote does not accept flick");
            continue;
        }
        if (name == "arc" || name == "hold" || name == "camera" || name == "rgb" || name.empty()) {
            append(RewriteTimedEvent(*call, line_number, *state.diagnostics));
            continue;
        }
        AddDiag(*state.diagnostics, line_number, call->name, "DROPPED_COMMAND",
                "unsupported AFF command");
    }
    if (header) {
        if (out.empty()) append("AudioOffset:0");
        append("-");
        AddDiag(*state.diagnostics, line_number, source_name, "REWRITTEN",
                "inserted missing AFF header terminator");
    }
    while (open_groups > 0) {
        append("};");
        --open_groups;
        AddDiag(*state.diagnostics, line_number, source_name, "REWRITTEN",
                "closed unclosed timinggroup");
    }
    if (!out.empty() && out.back() != '\n') out.push_back('\n');
    return out;
}

} // namespace

Result Normalize(std::string_view text, std::string_view source_name, const Source *files) {
    Result result;
    NormalizeState state;
    state.files = files;
    state.diagnostics = &result.diagnostics;
    if (!source_name.empty()) state.active.insert(std::string(source_name));
    result.text = NormalizeDocument(text, source_name, state);
    return result;
}

} // namespace arc_helper::aff
