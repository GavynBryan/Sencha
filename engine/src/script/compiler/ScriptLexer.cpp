#include "ScriptLexer.h"

#include <array>
#include <cctype>

namespace
{
    struct Keyword
    {
        std::string_view Text;
        ScriptTokKind Kind;
    };

    constexpr Keyword kKeywords[] = {
        {"component", ScriptTokKind::KwComponent},
        {"enum", ScriptTokKind::KwEnum},
        {"ability", ScriptTokKind::KwAbility},
        {"behavior", ScriptTokKind::KwBehavior},
        {"trigger", ScriptTokKind::KwTrigger},
        {"interaction", ScriptTokKind::KwInteraction},
        {"state", ScriptTokKind::KwState},
        {"fn", ScriptTokKind::KwFn},
        {"const", ScriptTokKind::KwConst},
        {"param", ScriptTokKind::KwParam},
        {"import", ScriptTokKind::KwImport},
        {"let", ScriptTokKind::KwLet},
        {"if", ScriptTokKind::KwIf},
        {"else", ScriptTokKind::KwElse},
        {"while", ScriptTokKind::KwWhile},
        {"for", ScriptTokKind::KwFor},
        {"in", ScriptTokKind::KwIn},
        {"return", ScriptTokKind::KwReturn},
        {"enter", ScriptTokKind::KwEnter},
        {"true", ScriptTokKind::KwTrue},
        {"false", ScriptTokKind::KwFalse},
    };

    // tag"..." style literal prefixes.
    struct LitPrefix
    {
        std::string_view Text;
        ScriptTokKind Kind;
    };

    constexpr LitPrefix kLitPrefixes[] = {
        {"tag", ScriptTokKind::TagLit},
        {"cue", ScriptTokKind::CueLit},
        {"prefab", ScriptTokKind::PrefabLit},
        {"asset", ScriptTokKind::AssetLit},
    };

    [[nodiscard]] bool IsIdentStart(char c)
    {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }

    [[nodiscard]] bool IsIdentChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    // Continuation suffix: a line ending on one of these does not terminate
    // the statement.
    [[nodiscard]] bool IsContinuationSuffix(ScriptTokKind kind)
    {
        switch (kind)
        {
        case ScriptTokKind::Plus: case ScriptTokKind::Minus:
        case ScriptTokKind::Star: case ScriptTokKind::Slash:
        case ScriptTokKind::Percent:
        case ScriptTokKind::AndAnd: case ScriptTokKind::OrOr:
        case ScriptTokKind::EqEq: case ScriptTokKind::NotEq:
        case ScriptTokKind::Lt: case ScriptTokKind::Le:
        case ScriptTokKind::Gt: case ScriptTokKind::Ge:
        case ScriptTokKind::Assign:
        case ScriptTokKind::PlusAssign: case ScriptTokKind::MinusAssign:
        case ScriptTokKind::StarAssign: case ScriptTokKind::SlashAssign:
        case ScriptTokKind::Comma: case ScriptTokKind::Dot:
        case ScriptTokKind::DotDot:
        case ScriptTokKind::LParen: case ScriptTokKind::LBracket:
        case ScriptTokKind::LBrace: case ScriptTokKind::Colon:
        case ScriptTokKind::Not:
            return true;
        default:
            return false;
        }
    }

    // Continuation prefix: a line beginning with one of these continues the
    // previous statement.
    [[nodiscard]] bool IsContinuationPrefix(ScriptTokKind kind)
    {
        switch (kind)
        {
        case ScriptTokKind::Dot:
        case ScriptTokKind::Plus: case ScriptTokKind::Minus:
        case ScriptTokKind::Star: case ScriptTokKind::Slash:
        case ScriptTokKind::Percent:
        case ScriptTokKind::AndAnd: case ScriptTokKind::OrOr:
        case ScriptTokKind::EqEq: case ScriptTokKind::NotEq:
        case ScriptTokKind::Lt: case ScriptTokKind::Le:
        case ScriptTokKind::Gt: case ScriptTokKind::Ge:
            return true;
        default:
            return false;
        }
    }
}

ScriptLexResult LexScript(std::string_view source)
{
    ScriptLexResult result;

    struct Raw
    {
        ScriptToken Token;
        bool NewlineBefore = false;
    };
    std::vector<Raw> raw;

    uint32_t line = 1;
    uint32_t col = 1;
    size_t i = 0;
    bool newlinePending = false;

    auto fail = [&result](uint32_t atLine, uint32_t atCol, std::string message) {
        result.Ok = false;
        result.Error = std::move(message);
        result.ErrorLine = atLine;
        result.ErrorCol = atCol;
    };

    auto push = [&raw, &newlinePending](ScriptTokKind kind, std::string_view text,
                                        uint32_t atLine, uint32_t atCol) {
        raw.push_back({{kind, text, atLine, atCol}, newlinePending});
        newlinePending = false;
    };

    while (i < source.size())
    {
        const char c = source[i];
        if (c == '\n')
        {
            newlinePending = true;
            ++i;
            ++line;
            col = 1;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r')
        {
            ++i;
            ++col;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/')
        {
            while (i < source.size() && source[i] != '\n')
            {
                ++i;
            }
            continue;
        }

        const uint32_t tokLine = line;
        const uint32_t tokCol = col;

        if (IsIdentStart(c))
        {
            size_t end = i;
            while (end < source.size() && IsIdentChar(source[end]))
            {
                ++end;
            }
            const std::string_view text = source.substr(i, end - i);
            col += static_cast<uint32_t>(end - i);
            i = end;

            // tag"..." / cue"..." and friends: prefix immediately followed
            // by a string literal.
            bool isLitPrefix = false;
            if (i < source.size() && source[i] == '"')
            {
                for (const LitPrefix& prefix : kLitPrefixes)
                {
                    if (text == prefix.Text)
                    {
                        size_t send = i + 1;
                        while (send < source.size() && source[send] != '"' && source[send] != '\n')
                        {
                            ++send;
                        }
                        if (send >= source.size() || source[send] != '"')
                        {
                            fail(tokLine, tokCol, "unterminated literal string");
                            return result;
                        }
                        push(prefix.Kind, source.substr(i + 1, send - i - 1), tokLine, tokCol);
                        col += static_cast<uint32_t>(send + 1 - i);
                        i = send + 1;
                        isLitPrefix = true;
                        break;
                    }
                }
            }
            if (isLitPrefix)
            {
                continue;
            }

            ScriptTokKind kind = ScriptTokKind::Ident;
            for (const Keyword& keyword : kKeywords)
            {
                if (text == keyword.Text)
                {
                    kind = keyword.Kind;
                    break;
                }
            }
            push(kind, text, tokLine, tokCol);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t end = i;
            bool isFloat = false;
            while (end < source.size()
                   && (std::isdigit(static_cast<unsigned char>(source[end])) || source[end] == '.'
                       || source[end] == 'e' || source[end] == 'E'
                       || ((source[end] == '+' || source[end] == '-') && end > i
                           && (source[end - 1] == 'e' || source[end - 1] == 'E'))))
            {
                if (source[end] == '.')
                {
                    // ".." is a range, not a float.
                    if (end + 1 < source.size() && source[end + 1] == '.')
                    {
                        break;
                    }
                    isFloat = true;
                }
                if (source[end] == 'e' || source[end] == 'E')
                {
                    isFloat = true;
                }
                ++end;
            }
            push(isFloat ? ScriptTokKind::FloatLit : ScriptTokKind::IntLit,
                 source.substr(i, end - i), tokLine, tokCol);
            col += static_cast<uint32_t>(end - i);
            i = end;
            continue;
        }

        if (c == '"')
        {
            size_t send = i + 1;
            while (send < source.size() && source[send] != '"' && source[send] != '\n')
            {
                ++send;
            }
            if (send >= source.size() || source[send] != '"')
            {
                fail(tokLine, tokCol, "unterminated string");
                return result;
            }
            push(ScriptTokKind::StringLit, source.substr(i + 1, send - i - 1), tokLine, tokCol);
            col += static_cast<uint32_t>(send + 1 - i);
            i = send + 1;
            continue;
        }

        auto twoChar = [&](char second) {
            return i + 1 < source.size() && source[i + 1] == second;
        };

        ScriptTokKind kind = ScriptTokKind::Eof;
        uint32_t width = 1;
        switch (c)
        {
        case '{': kind = ScriptTokKind::LBrace; break;
        case '}': kind = ScriptTokKind::RBrace; break;
        case '(': kind = ScriptTokKind::LParen; break;
        case ')': kind = ScriptTokKind::RParen; break;
        case '[': kind = ScriptTokKind::LBracket; break;
        case ']': kind = ScriptTokKind::RBracket; break;
        case ',': kind = ScriptTokKind::Comma; break;
        case ':': kind = ScriptTokKind::Colon; break;
        case ';': kind = ScriptTokKind::Terminator; break;
        case '.':
            if (twoChar('.')) { kind = ScriptTokKind::DotDot; width = 2; }
            else { kind = ScriptTokKind::Dot; }
            break;
        case '+':
            if (twoChar('=')) { kind = ScriptTokKind::PlusAssign; width = 2; }
            else { kind = ScriptTokKind::Plus; }
            break;
        case '-':
            if (twoChar('=')) { kind = ScriptTokKind::MinusAssign; width = 2; }
            else if (twoChar('>')) { kind = ScriptTokKind::Arrow; width = 2; }
            else { kind = ScriptTokKind::Minus; }
            break;
        case '*':
            if (twoChar('=')) { kind = ScriptTokKind::StarAssign; width = 2; }
            else { kind = ScriptTokKind::Star; }
            break;
        case '/':
            if (twoChar('=')) { kind = ScriptTokKind::SlashAssign; width = 2; }
            else { kind = ScriptTokKind::Slash; }
            break;
        case '%': kind = ScriptTokKind::Percent; break;
        case '!':
            if (twoChar('=')) { kind = ScriptTokKind::NotEq; width = 2; }
            else { kind = ScriptTokKind::Not; }
            break;
        case '&':
            if (twoChar('&')) { kind = ScriptTokKind::AndAnd; width = 2; }
            else { fail(tokLine, tokCol, "unexpected character '&'"); return result; }
            break;
        case '|':
            if (twoChar('|')) { kind = ScriptTokKind::OrOr; width = 2; }
            else { fail(tokLine, tokCol, "unexpected character '|'"); return result; }
            break;
        case '=':
            if (twoChar('=')) { kind = ScriptTokKind::EqEq; width = 2; }
            else { kind = ScriptTokKind::Assign; }
            break;
        case '<':
            if (twoChar('=')) { kind = ScriptTokKind::Le; width = 2; }
            else { kind = ScriptTokKind::Lt; }
            break;
        case '>':
            if (twoChar('=')) { kind = ScriptTokKind::Ge; width = 2; }
            else { kind = ScriptTokKind::Gt; }
            break;
        default:
            fail(tokLine, tokCol, std::string("unexpected character '") + c + "'");
            return result;
        }
        push(kind, source.substr(i, width), tokLine, tokCol);
        i += width;
        col += width;
    }

    // Terminator insertion: walk the raw stream tracking paren
    // and bracket depth; braces do not suppress terminators (multi-line
    // record literals and argument lists continue via trailing commas).
    int depth = 0;
    for (size_t t = 0; t < raw.size(); ++t)
    {
        const Raw& entry = raw[t];
        const ScriptTokKind kind = entry.Token.Kind;
        if (entry.NewlineBefore && depth == 0 && !result.Tokens.empty())
        {
            const ScriptTokKind previous = result.Tokens.back().Kind;
            const bool suppressed = IsContinuationSuffix(previous)
                                    || IsContinuationPrefix(kind)
                                    || previous == ScriptTokKind::Terminator;
            if (!suppressed)
            {
                result.Tokens.push_back({ScriptTokKind::Terminator, {},
                                         entry.Token.Line, entry.Token.Col});
            }
        }
        if (kind == ScriptTokKind::LParen || kind == ScriptTokKind::LBracket)
        {
            ++depth;
        }
        else if (kind == ScriptTokKind::RParen || kind == ScriptTokKind::RBracket)
        {
            depth = depth > 0 ? depth - 1 : 0;
        }
        if (kind == ScriptTokKind::Terminator && !result.Tokens.empty()
            && result.Tokens.back().Kind == ScriptTokKind::Terminator)
        {
            continue; // collapse runs
        }
        result.Tokens.push_back(entry.Token);
    }
    result.Tokens.push_back({ScriptTokKind::Eof, {}, line, col});
    result.Ok = true;
    return result;
}
