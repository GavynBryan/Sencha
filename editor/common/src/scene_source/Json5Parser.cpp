#include "scene_source/Json5Parser.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace
{
    // Deep enough for any authored scene, shallow enough that a malicious or
    // corrupt file fails with a message instead of a stack overflow.
    constexpr int kMaxDepth = 256;

    struct Cursor
    {
        std::string_view Text;
        std::size_t At = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool AtEnd() const { return At >= Text.size(); }
        [[nodiscard]] char Peek(std::size_t ahead = 0) const
        {
            return At + ahead < Text.size() ? Text[At + ahead] : '\0';
        }
        [[nodiscard]] bool Starts(std::string_view prefix) const
        {
            return Text.substr(At, prefix.size()) == prefix;
        }

        char Take()
        {
            const char c = Text[At++];
            if (c == '\n')
            {
                ++Line;
                Column = 1;
            }
            else
            {
                ++Column;
            }
            return c;
        }

        void Take(std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
                Take();
        }
    };

    class Parser
    {
    public:
        Parser(std::string_view text, Json5ParseError* error)
            : At{ text }
            , Error(error)
        {
        }

        [[nodiscard]] std::optional<Json5Value> Run(std::vector<std::string>* endComments)
        {
            std::vector<std::string> leading;
            if (!SkipTrivia(leading))
                return std::nullopt;
            if (At.AtEnd())
                return Fail("document is empty");

            Json5Value root;
            if (!ParseValue(root, 0))
                return std::nullopt;
            root.LeadingComments.insert(root.LeadingComments.begin(),
                                        leading.begin(), leading.end());

            std::vector<std::string> trailing;
            if (!SkipTrivia(trailing))
                return std::nullopt;
            if (!At.AtEnd())
                return Fail("unexpected content after the document value");
            if (endComments != nullptr)
                *endComments = std::move(trailing);
            return root;
        }

    private:
        Cursor At;
        Json5ParseError* Error = nullptr;

        std::nullopt_t Fail(std::string message)
        {
            if (Error != nullptr)
                *Error = Json5ParseError{ At.Line, At.Column, std::move(message) };
            return std::nullopt;
        }

        bool FailBool(std::string message)
        {
            (void)Fail(std::move(message));
            return false;
        }

        // ── Trivia ───────────────────────────────────────────────────────────

        [[nodiscard]] bool IsLineTerminator() const
        {
            const char c = At.Peek();
            if (c == '\n' || c == '\r')
                return true;
            // U+2028 / U+2029 in UTF-8.
            return static_cast<unsigned char>(c) == 0xE2
                && static_cast<unsigned char>(At.Peek(1)) == 0x80
                && (static_cast<unsigned char>(At.Peek(2)) == 0xA8
                    || static_cast<unsigned char>(At.Peek(2)) == 0xA9);
        }

        [[nodiscard]] bool IsWhitespaceHere() const
        {
            const char c = At.Peek();
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f'
                || c == '\n' || c == '\r')
                return true;
            const auto b0 = static_cast<unsigned char>(c);
            const auto b1 = static_cast<unsigned char>(At.Peek(1));
            const auto b2 = static_cast<unsigned char>(At.Peek(2));
            if (b0 == 0xC2 && b1 == 0xA0) // U+00A0 no-break space
                return true;
            if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) // U+FEFF BOM
                return true;
            return b0 == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9);
        }

        // Consumes whitespace and comments; comments append to `comments`
        // verbatim, delimiters included.
        [[nodiscard]] bool SkipTrivia(std::vector<std::string>& comments)
        {
            while (!At.AtEnd())
            {
                if (IsWhitespaceHere())
                {
                    if (At.Peek() == '\n' || At.Peek() == '\r')
                        At.Take();
                    else if (static_cast<unsigned char>(At.Peek()) < 0x80)
                        At.Take();
                    else if (static_cast<unsigned char>(At.Peek()) == 0xC2)
                        At.Take(2);
                    else
                        At.Take(3);
                    continue;
                }
                if (At.Peek() == '/' && At.Peek(1) == '/')
                {
                    const std::size_t start = At.At;
                    while (!At.AtEnd() && !IsLineTerminator())
                        At.Take();
                    comments.emplace_back(At.Text.substr(start, At.At - start));
                    continue;
                }
                if (At.Peek() == '/' && At.Peek(1) == '*')
                {
                    const std::size_t start = At.At;
                    At.Take(2);
                    while (!At.AtEnd() && !At.Starts("*/"))
                        At.Take();
                    if (At.AtEnd())
                        return FailBool("unterminated block comment");
                    At.Take(2);
                    comments.emplace_back(At.Text.substr(start, At.At - start));
                    continue;
                }
                break;
            }
            return true;
        }

        // ── Values ───────────────────────────────────────────────────────────

        [[nodiscard]] bool ParseValue(Json5Value& out, int depth)
        {
            if (depth > kMaxDepth)
                return FailBool("nesting exceeds the depth limit");

            const char c = At.Peek();
            if (c == '{')
                return ParseObject(out, depth);
            if (c == '[')
                return ParseArray(out, depth);
            if (c == '"' || c == '\'')
            {
                out.K = Json5Value::Kind::String;
                return ParseString(out.Text);
            }
            if (c == 't' || c == 'f' || c == 'n' || c == 'I' || c == 'N'
                || c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c)))
            {
                return ParseWord(out);
            }
            return FailBool(std::string("unexpected character '") + c + "'");
        }

        [[nodiscard]] bool ParseObject(Json5Value& out, int depth)
        {
            out.K = Json5Value::Kind::Object;
            At.Take(); // {
            // Comments between a value and its comma anchor forward to whatever
            // follows -- the next member, or the closing brace -- per the
            // "trivia precedes what follows" rule.
            std::vector<std::string> carried;
            while (true)
            {
                std::vector<std::string> comments = std::move(carried);
                carried.clear();
                if (!SkipTrivia(comments))
                    return false;
                if (At.AtEnd())
                    return FailBool("unterminated object");
                if (At.Peek() == '}')
                {
                    At.Take();
                    out.TrailingComments = std::move(comments);
                    return true;
                }

                std::string key;
                if (At.Peek() == '"' || At.Peek() == '\'')
                {
                    if (!ParseString(key))
                        return false;
                }
                else if (!ParseIdentifier(key))
                {
                    return false;
                }
                if (out.Find(key) != nullptr)
                    return FailBool("duplicate key '" + key
                        + "': a duplicate in authored content is a merge "
                          "accident about to lose someone's work");

                std::vector<std::string> between;
                if (!SkipTrivia(between))
                    return false;
                if (At.Peek() != ':')
                    return FailBool("expected ':' after key '" + key + "'");
                At.Take();
                if (!SkipTrivia(between))
                    return false;

                Json5Value value;
                if (!ParseValue(value, depth + 1))
                    return false;
                value.LeadingComments = std::move(comments);
                value.LeadingComments.insert(value.LeadingComments.end(),
                                             between.begin(), between.end());
                out.Members.emplace_back(std::move(key), std::move(value));

                std::vector<std::string> after;
                if (!SkipTrivia(after))
                    return false;
                if (At.Peek() == ',')
                {
                    At.Take();
                    carried = std::move(after);
                    continue;
                }
                if (At.Peek() == '}')
                {
                    At.Take();
                    out.TrailingComments = std::move(after);
                    return true;
                }
                return FailBool("expected ',' or '}' in object");
            }
        }

        [[nodiscard]] bool ParseArray(Json5Value& out, int depth)
        {
            out.K = Json5Value::Kind::Array;
            At.Take(); // [
            std::vector<std::string> carried;
            while (true)
            {
                std::vector<std::string> comments = std::move(carried);
                carried.clear();
                if (!SkipTrivia(comments))
                    return false;
                if (At.AtEnd())
                    return FailBool("unterminated array");
                if (At.Peek() == ']')
                {
                    At.Take();
                    out.TrailingComments = std::move(comments);
                    return true;
                }

                Json5Value element;
                if (!ParseValue(element, depth + 1))
                    return false;
                element.LeadingComments = std::move(comments);
                out.Elements.push_back(std::move(element));

                std::vector<std::string> after;
                if (!SkipTrivia(after))
                    return false;
                if (At.Peek() == ',')
                {
                    At.Take();
                    carried = std::move(after);
                    continue;
                }
                if (At.Peek() == ']')
                {
                    At.Take();
                    out.TrailingComments = std::move(after);
                    return true;
                }
                return FailBool("expected ',' or ']' in array");
            }
        }

        [[nodiscard]] bool ParseIdentifier(std::string& out)
        {
            const char first = At.Peek();
            if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_' || first == '$'))
            {
                if (static_cast<unsigned char>(first) >= 0x80)
                    return FailBool("non-ASCII keys must be quoted");
                return FailBool("expected a key");
            }
            const std::size_t start = At.At;
            while (!At.AtEnd())
            {
                const char c = At.Peek();
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')
                    At.Take();
                else
                    break;
            }
            out.assign(At.Text.substr(start, At.At - start));
            return true;
        }

        [[nodiscard]] bool ParseWord(Json5Value& out)
        {
            if (At.Starts("true"))
            {
                At.Take(4);
                out = Json5Value(true);
                return true;
            }
            if (At.Starts("false"))
            {
                At.Take(5);
                out = Json5Value(false);
                return true;
            }
            if (At.Starts("null"))
            {
                At.Take(4);
                out = Json5Value{};
                return true;
            }
            return ParseNumber(out);
        }

        [[nodiscard]] bool ParseNumber(Json5Value& out)
        {
            const std::size_t start = At.At;
            if (At.Peek() == '+' || At.Peek() == '-')
                At.Take();

            if (At.Starts("Infinity") || At.Starts("NaN"))
                return FailBool("non-finite numbers are not valid scene data");

            bool hex = false;
            if (At.Peek() == '0' && (At.Peek(1) == 'x' || At.Peek(1) == 'X'))
            {
                hex = true;
                At.Take(2);
                if (!std::isxdigit(static_cast<unsigned char>(At.Peek())))
                    return FailBool("expected hex digits after 0x");
                while (std::isxdigit(static_cast<unsigned char>(At.Peek())))
                    At.Take();
            }
            else
            {
                bool digits = false;
                while (std::isdigit(static_cast<unsigned char>(At.Peek())))
                {
                    At.Take();
                    digits = true;
                }
                if (At.Peek() == '.')
                {
                    At.Take();
                    while (std::isdigit(static_cast<unsigned char>(At.Peek())))
                    {
                        At.Take();
                        digits = true;
                    }
                }
                if (!digits)
                    return FailBool("malformed number");
                if (At.Peek() == 'e' || At.Peek() == 'E')
                {
                    At.Take();
                    if (At.Peek() == '+' || At.Peek() == '-')
                        At.Take();
                    if (!std::isdigit(static_cast<unsigned char>(At.Peek())))
                        return FailBool("malformed exponent");
                    while (std::isdigit(static_cast<unsigned char>(At.Peek())))
                        At.Take();
                }
            }

            const std::string_view lexeme = At.Text.substr(start, At.At - start);
            double value = 0.0;
            if (hex)
            {
                const bool negative = lexeme.front() == '-';
                const std::size_t skip = (lexeme.front() == '+' || negative ? 1u : 0u) + 2u;
                std::uint64_t raw = 0;
                const auto digits = lexeme.substr(skip);
                const auto result =
                    std::from_chars(digits.data(), digits.data() + digits.size(), raw, 16);
                if (result.ec != std::errc{})
                    return FailBool("hex number out of range");
                value = negative ? -static_cast<double>(raw) : static_cast<double>(raw);
            }
            else
            {
                // strtod accepts the JSON5 decimal forms (leading '+',
                // leading/trailing '.') that from_chars does not.
                const std::string owned(lexeme);
                char* end = nullptr;
                value = std::strtod(owned.c_str(), &end);
                if (end != owned.c_str() + owned.size())
                    return FailBool("malformed number");
                if (!std::isfinite(value))
                    return FailBool("number overflows to non-finite");
            }
            out = Json5Value(value);
            return true;
        }

        [[nodiscard]] bool ParseString(std::string& out)
        {
            const char quote = At.Take();
            out.clear();
            while (true)
            {
                if (At.AtEnd())
                    return FailBool("unterminated string");
                if (At.Peek() == quote)
                {
                    At.Take();
                    return true;
                }
                if (At.Peek() == '\n' || At.Peek() == '\r')
                    return FailBool("unescaped line break in string");
                if (At.Peek() != '\\')
                {
                    out.push_back(At.Take());
                    continue;
                }

                At.Take(); // backslash
                if (At.AtEnd())
                    return FailBool("unterminated escape");

                // Line continuation: an escaped terminator joins the lines.
                if (At.Peek() == '\n' || At.Peek() == '\r')
                {
                    const char first = At.Take();
                    if (first == '\r' && At.Peek() == '\n')
                        At.Take();
                    continue;
                }
                if (IsLineTerminator())
                {
                    At.Take(3); // LS/PS
                    continue;
                }

                const char c = At.Take();
                switch (c)
                {
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'v': out.push_back('\v'); break;
                case '0':
                    if (std::isdigit(static_cast<unsigned char>(At.Peek())))
                        return FailBool("\\0 must not be followed by a digit");
                    out.push_back('\0');
                    break;
                case 'x':
                {
                    unsigned int code = 0;
                    if (!TakeHex(2, code))
                        return false;
                    AppendUtf8(out, code);
                    break;
                }
                case 'u':
                {
                    unsigned int code = 0;
                    if (!TakeHex(4, code))
                        return false;
                    // Surrogate pair: a high surrogate must pair with a low one.
                    if (code >= 0xD800 && code <= 0xDBFF && At.Peek() == '\\'
                        && At.Peek(1) == 'u')
                    {
                        At.Take(2);
                        unsigned int low = 0;
                        if (!TakeHex(4, low))
                            return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return FailBool("invalid low surrogate");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    if (std::isdigit(static_cast<unsigned char>(c)))
                        return FailBool("numeric escapes other than \\0 are not valid");
                    out.push_back(c); // any other escaped character is itself
                    break;
                }
            }
        }

        [[nodiscard]] bool TakeHex(int count, unsigned int& out)
        {
            out = 0;
            for (int i = 0; i < count; ++i)
            {
                const char c = At.Peek();
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                    return FailBool("malformed hex escape");
                At.Take();
                out = out * 16
                    + static_cast<unsigned int>(
                        std::isdigit(static_cast<unsigned char>(c))
                            ? c - '0'
                            : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
            }
            return true;
        }

        static void AppendUtf8(std::string& out, unsigned int code)
        {
            if (code < 0x80)
            {
                out.push_back(static_cast<char>(code));
            }
            else if (code < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else if (code < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }
    };
} // namespace

std::optional<Json5Value> Json5Parse(std::string_view text,
                                     Json5ParseError* error,
                                     std::vector<std::string>* endComments)
{
    Parser parser(text, error);
    return parser.Run(endComments);
}
