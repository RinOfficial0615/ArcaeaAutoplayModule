#include "manager/custom_chart/AffOfficialParser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace arc_helper::aff {
namespace {

enum class Tok {
    End,
    Error,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Int,
    Float,
    Ident,
    Timing,
    Hold,
    Arc,
    Camera,
    Scenecontrol,
    Timinggroup,
    Flick,
    Arctap,
    True,
    False,
    Rgb,
    Designant,
};

// Single-character tokens: a hit in kPunctuation indexes kPunctuationTokens.
constexpr std::string_view kPunctuation = "(){}[],;";
constexpr std::array<Tok, kPunctuation.size()> kPunctuationTokens = {
    Tok::LParen, Tok::RParen, Tok::LBrace,   Tok::RBrace,
    Tok::LBracket, Tok::RBracket, Tok::Comma, Tok::Semicolon,
};

constexpr std::string_view kDigits = "0123456789";
constexpr std::string_view kIdentifierChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

using Keyword = std::pair<std::string_view, Tok>;
// Reserved words. Longest-first ordering is not needed: the lexer matches whole
// identifiers, so `arctap` and `at` never shadow each other.
constexpr std::array<Keyword, 13> kKeywords = {{
    {"timing", Tok::Timing},
    {"hold", Tok::Hold},
    {"arc", Tok::Arc},
    {"camera", Tok::Camera},
    {"scenecontrol", Tok::Scenecontrol},
    {"timinggroup", Tok::Timinggroup},
    {"flick", Tok::Flick},
    {"arctap", Tok::Arctap},
    {"at", Tok::Arctap},
    {"true", Tok::True},
    {"false", Tok::False},
    {"rgb", Tok::Rgb},
    {"designant", Tok::Designant},
}};

struct Lexer {
    std::string_view text;
    size_t i = 0;
    int line = 1;
    bool has_peek = false;
    Tok peeked = Tok::End;
    int peek_line = 1;

    void SkipSpace() {
        while (i < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == '\n') {
                ++line;
                ++i;
                continue;
            }
            if (c == '\r' || c == ' ' || c == '\t') {
                ++i;
                continue;
            }
            break;
        }
    }

    bool Take(char expected) {
        if (i < text.size() && text[i] == expected) {
            ++i;
            return true;
        }
        return false;
    }

    // find_first_not_of returns npos once the rest of the text is all digits;
    // clamping to size() makes that "consume to the end" instead of overflowing.
    std::string_view TakeDigits() {
        const size_t begin = i;
        i = std::min(text.find_first_not_of(kDigits, i), text.size());
        return text.substr(begin, i - begin);
    }

    Tok LexOne() {
        SkipSpace();
        if (i >= text.size()) return Tok::End;
        const char c = text[i];
        if (const size_t punct = kPunctuation.find(c); punct != std::string_view::npos) {
            ++i;
            return kPunctuationTokens[punct];
        }
        if (c == '-') {
            ++i;
            if (TakeDigits().empty() || !Take('.') || TakeDigits().empty()) return Tok::Error;
            return Tok::Float;
        }
        if (c == '.') {
            ++i;
            if (TakeDigits().empty()) return Tok::Error;
            return Tok::Float;
        }
        if (c >= '0' && c <= '9') {
            TakeDigits();
            if (Take('.')) {
                if (TakeDigits().empty()) return Tok::Error;
                return Tok::Float;
            }
            return Tok::Int;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t begin = i;
            i = std::min(text.find_first_not_of(kIdentifierChars, i), text.size());
            const std::string_view ident = text.substr(begin, i - begin);
            const auto keyword = std::ranges::find(kKeywords, ident, &Keyword::first);
            return keyword == kKeywords.end() ? Tok::Ident : keyword->second;
        }
        ++i;
        return Tok::Error;
    }

    Tok Peek() {
        if (!has_peek) {
            peeked = LexOne();
            peek_line = line;
            has_peek = true;
        }
        return peeked;
    }

    Tok Next() {
        const Tok tok = Peek();
        has_peek = false;
        return tok;
    }
};

struct Parser {
    Lexer lex;
    OfficialCheck fail;

    bool Error(std::string message) {
        fail.ok = false;
        fail.line = lex.has_peek ? lex.peek_line : lex.line;
        fail.error = std::move(message);
        return false;
    }

    bool Expect(Tok wanted, const char *what) {
        if (lex.Next() == wanted) return true;
        return Error(std::string("expected ") + what);
    }

    bool ParseNumber() {
        const Tok tok = lex.Next();
        if (tok == Tok::Int || tok == Tok::Float) return true;
        return Error("expected int or float");
    }

    bool ParseTiming() {
        return Expect(Tok::LParen, "(") && Expect(Tok::Int, "int timing") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Float, "float bpm") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Float, "float divisor") &&
               Expect(Tok::RParen, ")");
    }

    bool ParseHold() {
        return Expect(Tok::LParen, "(") && Expect(Tok::Int, "int timing") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Int, "int end") &&
               Expect(Tok::Comma, ",") && ParseNumber() && Expect(Tok::RParen, ")");
    }

    bool ParseTap() {
        return Expect(Tok::Int, "int timing") && Expect(Tok::Comma, ",") && ParseNumber() &&
               Expect(Tok::RParen, ")");
    }

    bool ParseCamera() {
        return Expect(Tok::LParen, "(") && Expect(Tok::Int, "int timing") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") &&
               Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") &&
               Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Ident, "camera easing") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Int, "int duration") &&
               Expect(Tok::RParen, ")");
    }

    bool ParseScenecontrol() {
        if (!Expect(Tok::LParen, "(") || !Expect(Tok::Int, "int timing") ||
            !Expect(Tok::Comma, ",") || !Expect(Tok::Ident, "scenecontrol type")) {
            return false;
        }
        if (lex.Peek() == Tok::RParen) {
            lex.Next();
            return true;
        }
        return Expect(Tok::Comma, ",") && Expect(Tok::Float, "float argument") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Int, "int argument") &&
               Expect(Tok::RParen, ")");
    }

    bool ParseRgb() {
        return Expect(Tok::LParen, "(") && Expect(Tok::Int, "int") && Expect(Tok::Comma, ",") &&
               Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") &&
               Expect(Tok::Comma, ",") && Expect(Tok::Float, "float") && Expect(Tok::Comma, ",") &&
               Expect(Tok::Float, "float") && Expect(Tok::RParen, ")");
    }

    bool ParseArcTaps() {
        if (lex.Peek() != Tok::LBracket) return true;
        lex.Next();
        while (true) {
            if (!Expect(Tok::Arctap, "arctap") || !Expect(Tok::LParen, "(") ||
                !Expect(Tok::Int, "int arctap timing") || !Expect(Tok::RParen, ")")) {
                return false;
            }
            const Tok next = lex.Peek();
            if (next == Tok::RBracket) {
                lex.Next();
                return true;
            }
            if (next != Tok::Comma) return Error("expected either `]` or `,`");
            lex.Next();
        }
    }

    bool ParseArc() {
        if (!Expect(Tok::LParen, "(") || !Expect(Tok::Int, "int timing") ||
            !Expect(Tok::Comma, ",") || !Expect(Tok::Int, "int end") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Float, "float x1") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Float, "float x2") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Ident, "arc easing") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Float, "float y1") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Float, "float y2") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Int, "int color") || !Expect(Tok::Comma, ",") ||
            !Expect(Tok::Ident, "arc sfx")) {
            return false;
        }
        if (lex.Peek() != Tok::Comma) return Error("expected , before istrace");
        lex.Next();
        const Tok flag = lex.Next();
        if (flag != Tok::True && flag != Tok::False && flag != Tok::Designant) {
            return Error("expected true, false, or designant");
        }
        if (lex.Peek() == Tok::Comma) {
            lex.Next();
            if (!Expect(Tok::Float, "float arc resolution")) return false;
        }
        return Expect(Tok::RParen, ")") && ParseArcTaps();
    }

    bool ParseTimingGroup() {
        if (!Expect(Tok::LParen, "(")) return false;
        if (lex.Peek() == Tok::Ident) lex.Next();
        if (!Expect(Tok::RParen, ")") || !Expect(Tok::LBrace, "{")) return false;
        while (lex.Peek() != Tok::RBrace && lex.Peek() != Tok::End && lex.Peek() != Tok::Error) {
            if (!ParseEvent() || !Expect(Tok::Semicolon, ";")) return false;
        }
        return Expect(Tok::RBrace, "}");
    }

    bool ParseEvent() {
        const Tok tok = lex.Next();
        switch (tok) {
        case Tok::Timing:
            return ParseTiming();
        case Tok::Hold:
            return ParseHold();
        case Tok::Arc:
            return ParseArc();
        case Tok::Camera:
            return ParseCamera();
        case Tok::Scenecontrol:
            return ParseScenecontrol();
        case Tok::Timinggroup:
            return ParseTimingGroup();
        case Tok::Rgb:
            return ParseRgb();
        case Tok::LParen:
            return ParseTap();
        case Tok::Error:
            return Error("invalid token");
        case Tok::Flick:
            return Error("unparsable note starting with: flick");
        default:
            return Error("unparsable note");
        }
    }

    bool ParseBody() {
        while (lex.Peek() != Tok::End) {
            if (lex.Peek() == Tok::Error) return Error("invalid token");
            if (!ParseEvent() || !Expect(Tok::Semicolon, ";")) return false;
        }
        return true;
    }
};

std::string_view TrimLine(std::string_view line) {
    constexpr std::string_view kBlanks = " \t\r";
    const size_t begin = line.find_first_not_of(kBlanks);
    if (begin == std::string_view::npos) return {};
    return line.substr(begin, line.find_last_not_of(kBlanks) - begin + 1);
}

} // namespace

OfficialCheck CheckOfficial(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    size_t i = 0;
    int line = 1;
    bool header = true;
    bool saw_offset = false;
    while (header && i < text.size()) {
        const size_t begin = i;
        while (i < text.size() && text[i] != '\n') ++i;
        const std::string_view raw = text.substr(begin, i - begin);
        if (i < text.size()) ++i;
        const std::string_view trimmed = TrimLine(raw);
        if (trimmed.empty()) {
            ++line;
            continue;
        }
        if (trimmed == "-") {
            header = false;
            ++line;
            break;
        }
        const size_t colon = trimmed.find(':');
        if (colon == std::string_view::npos) {
            return {false, line, "invalid header line"};
        }
        const auto key = TrimLine(trimmed.substr(0, colon));
        if (key == "AudioOffset") saw_offset = true;
        else if (key != "TimingPointDensityFactor") {
            return {false, line, "unknown AFF header"};
        }
        ++line;
    }
    if (header) return {false, line, "missing AFF header terminator"};
    if (!saw_offset) return {false, line, "missing AudioOffset"};
    Parser parser;
    parser.lex.text = text.substr(i);
    parser.lex.line = line;
    if (!parser.ParseBody()) return parser.fail;
    return {};
}

} // namespace arc_helper::aff
