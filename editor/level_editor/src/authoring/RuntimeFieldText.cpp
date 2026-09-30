#include "RuntimeFieldText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <vector>

namespace
{
// Enough for any leaf the schema flattens: a Transform's widest member is a
// four-scalar rotation, and a leaf wider than this would be a composite the
// collector should have recursed through instead.
constexpr std::size_t kMaxScalars = 16;

constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi;

[[nodiscard]] bool IsScalarKind(const RuntimeField& field)
{
    return field.Scalar != FieldScalar::Unsupported;
}

[[nodiscard]] const std::byte* At(const void* base, std::size_t offset)
{
    return static_cast<const std::byte*>(base) + offset;
}

[[nodiscard]] std::byte* At(void* base, std::size_t offset)
{
    return static_cast<std::byte*>(base) + offset;
}

// Integer leaves are read and written through their declared size and
// signedness so an edit never over- or under-writes the component's bytes.
[[nodiscard]] std::int64_t ReadInteger(const void* ptr, const RuntimeField& field)
{
    const bool isSigned = field.Scalar == FieldScalar::Int32;
    switch (field.Size)
    {
    case 1:
        return isSigned ? static_cast<std::int64_t>(*static_cast<const std::int8_t*>(ptr))
                        : static_cast<std::int64_t>(*static_cast<const std::uint8_t*>(ptr));
    case 2:
        return isSigned ? static_cast<std::int64_t>(*static_cast<const std::int16_t*>(ptr))
                        : static_cast<std::int64_t>(*static_cast<const std::uint16_t*>(ptr));
    case 8:
    {
        std::int64_t value = 0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }
    default:
    {
        std::int32_t narrow = 0;
        std::memcpy(&narrow, ptr, sizeof(narrow));
        return isSigned ? static_cast<std::int64_t>(narrow)
                        : static_cast<std::int64_t>(static_cast<std::uint32_t>(narrow));
    }
    }
}

void WriteInteger(void* ptr, const RuntimeField& field, std::int64_t value)
{
    switch (field.Size)
    {
    case 1: { const auto narrow = static_cast<std::uint8_t>(value); std::memcpy(ptr, &narrow, 1); break; }
    case 2: { const auto narrow = static_cast<std::uint16_t>(value); std::memcpy(ptr, &narrow, 2); break; }
    case 8: std::memcpy(ptr, &value, 8); break;
    default: { const auto narrow = static_cast<std::uint32_t>(value); std::memcpy(ptr, &narrow, 4); break; }
    }
}

[[nodiscard]] double ReadFloating(const void* ptr, const RuntimeField& field)
{
    if (field.Scalar == FieldScalar::Double)
    {
        double value = 0.0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }
    float value = 0.0f;
    std::memcpy(&value, ptr, sizeof(value));
    return static_cast<double>(value);
}

void WriteFloating(void* ptr, const RuntimeField& field, double value)
{
    if (field.Scalar == FieldScalar::Double)
    {
        std::memcpy(ptr, &value, sizeof(value));
        return;
    }
    const auto narrow = static_cast<float>(value);
    std::memcpy(ptr, &narrow, sizeof(narrow));
}

// Six significant digits, with the trailing zeros and any orphaned point
// removed: a position reads "1.5" rather than "1.500000", and a value nobody
// touched re-reads as what it shows. The surface compares the text it published
// against the text that came back, so this losing the last bits of a float
// never turns an untouched field into an edit.
[[nodiscard]] std::string FormatScalar(double value)
{
    if (!std::isfinite(value))
        return "0";

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    std::string out(buffer);
    if (out.find('.') != std::string::npos && out.find('e') == std::string::npos
        && out.find('E') == std::string::npos)
    {
        out.erase(out.find_last_not_of('0') + 1);
        if (!out.empty() && out.back() == '.')
            out.pop_back();
    }
    if (out == "-0")
        out = "0";
    return out;
}

[[nodiscard]] std::string_view Trim(std::string_view text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

[[nodiscard]] constexpr char Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool EqualsIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) { return Lower(x) == Lower(y); });
}

// Exactly `count` comma-separated pieces. A vector edit that dropped or gained
// a component is a typo, not an instruction to leave the rest alone.
[[nodiscard]] bool SplitScalars(std::string_view text, std::size_t count,
                                std::array<std::string_view, kMaxScalars>& out)
{
    std::size_t found = 0;
    while (true)
    {
        if (found == count)
            return false; // more values than the field has scalars
        const std::size_t comma = text.find(',');
        if (comma == std::string_view::npos)
        {
            out[found++] = Trim(text);
            break;
        }
        out[found++] = Trim(text.substr(0, comma));
        text.remove_prefix(comma + 1);
    }
    return found == count;
}

[[nodiscard]] bool ParseFloating(std::string_view text, double& out)
{
    text = Trim(text);
    if (text.empty())
        return false;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool ParseIntegral(std::string_view text, std::int64_t& out)
{
    text = Trim(text);
    if (text.empty())
        return false;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    if (result.ec == std::errc{} && result.ptr == last)
        return true;

    // Unsigned 64-bit identities do not fit a signed parse. Re-read as unsigned
    // and carry the bits across, which is what the field holds anyway.
    std::uint64_t unsignedValue = 0;
    const std::from_chars_result wide = std::from_chars(first, last, unsignedValue);
    if (wide.ec != std::errc{} || wide.ptr != last)
        return false;
    out = static_cast<std::int64_t>(unsignedValue);
    return true;
}

[[nodiscard]] const EnumOption* FindEnumOption(const RuntimeField& field, std::int64_t value)
{
    for (const EnumOption& option : field.Enum)
    {
        if (option.Value == value)
            return &option;
    }
    return nullptr;
}

[[nodiscard]] bool IsFloatingKind(FieldScalar scalar)
{
    return scalar == FieldScalar::Float || scalar == FieldScalar::Double
        || scalar == FieldScalar::Color3;
}

// How many scalars a leaf presents, and how far apart they sit. A colour is one
// leaf whose Size spans all three floats, where a Vec3 is three scalars of
// Size 4 -- so neither the count nor the stride can be read off one member.
struct ScalarRun
{
    std::size_t Count = 1;
    std::size_t Stride = 0;
};

[[nodiscard]] ScalarRun RunOf(const RuntimeField& field)
{
    if (field.Scalar == FieldScalar::Color3)
        return { 3, sizeof(float) };
    return { std::max<std::size_t>(1, field.Count), field.Size };
}
} // namespace

std::string HumanizeSchemaName(std::string_view dotted)
{
    const std::size_t dot = dotted.find_last_of('.');
    std::string out(dot == std::string_view::npos ? dotted : dotted.substr(dot + 1));
    bool boundary = true;
    for (char& ch : out)
    {
        if (ch == '_')
        {
            ch = ' ';
            boundary = true;
            continue;
        }
        if (boundary && ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
        boundary = false;
    }
    return out;
}

std::string RuntimeFieldLabel(const RuntimeField& field)
{
    if (!field.Label.empty())
        return std::string(field.Label);
    return HumanizeSchemaName(field.Name);
}

bool IsRuntimeFieldEditable(const RuntimeField& field)
{
    // An asset handle is refcounted and session-local, so it never travels as
    // bytes; it needs a picker and the command that goes with one.
    if (field.ReadOnly || field.Asset != AssetType::Unknown)
        return false;
    return field.InlineText || IsScalarKind(field);
}

std::string FormatRuntimeField(const RuntimeField& field, const void* componentBytes)
{
    if (componentBytes == nullptr)
        return {};

    const std::byte* base = At(componentBytes, field.Offset);

    if (field.InlineText)
    {
        // Null-terminated and tail-zeroed by InlineString, so the terminator is
        // the length -- but bounded by the field anyway, because a buffer that
        // somehow lost its terminator must not be read past its own bytes.
        const char* text = reinterpret_cast<const char*>(base);
        return std::string(text, ::strnlen(text, field.Size));
    }

    if (!IsScalarKind(field))
        return {};

    if (!field.Enum.empty())
    {
        const std::int64_t value = ReadInteger(base, field);
        if (const EnumOption* option = FindEnumOption(field, value); option != nullptr)
            return std::string(option->Name);
        return FormatScalar(static_cast<double>(value));
    }

    if (field.Scalar == FieldScalar::Bool)
        return *reinterpret_cast<const bool*>(base) ? "true" : "false";

    const ScalarRun run = RunOf(field);
    std::string out;
    for (std::size_t i = 0; i < run.Count && i < kMaxScalars; ++i)
    {
        const std::byte* scalar = base + i * run.Stride;
        if (!out.empty())
            out += ", ";
        if (IsFloatingKind(field.Scalar))
        {
            double value = ReadFloating(scalar, field);
            if (field.DisplayDegrees)
                value *= kRadiansToDegrees;
            out += FormatScalar(value);
        }
        else if (field.Scalar == FieldScalar::UInt64 || field.Scalar == FieldScalar::UInt32)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%llu",
                          static_cast<unsigned long long>(ReadInteger(scalar, field)));
            out += buffer;
        }
        else
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld",
                          static_cast<long long>(ReadInteger(scalar, field)));
            out += buffer;
        }
    }
    return out;
}

bool ParseRuntimeField(const RuntimeField& field, std::string_view text,
                       void* componentBytes)
{
    if (componentBytes == nullptr || !IsRuntimeFieldEditable(field))
        return false;

    std::byte* base = At(componentBytes, field.Offset);

    if (field.InlineText)
    {
        // Refused rather than truncated. Everywhere else in the engine an
        // InlineString assignment truncates, which is right when the caller is
        // code; here the caller is a person watching their own characters
        // disappear, and being told the name is too long is the better answer.
        if (field.Size == 0 || text.size() + 1 > field.Size)
            return false;
        std::memcpy(base, text.data(), text.size());
        // The tail is zeroed, not just terminated: InlineString compares and
        // hashes the whole buffer, so leftover bytes from a longer previous
        // value would make two equal names unequal.
        std::memset(reinterpret_cast<char*>(base) + text.size(), 0,
                    field.Size - text.size());
        return true;
    }

    if (!field.Enum.empty())
    {
        const std::string_view name = Trim(text);
        for (const EnumOption& option : field.Enum)
        {
            if (EqualsIgnoringCase(option.Name, name)
                || EqualsIgnoringCase(option.Display, name))
            {
                WriteInteger(base, field, option.Value);
                return true;
            }
        }
        // A number is still a legitimate way to say which enumerator, but only
        // one the enum actually declares: writing an unnamed value would leave
        // the component holding something no schema can round-trip.
        std::int64_t numeric = 0;
        if (!ParseIntegral(text, numeric) || FindEnumOption(field, numeric) == nullptr)
            return false;
        WriteInteger(base, field, numeric);
        return true;
    }

    if (field.Scalar == FieldScalar::Bool)
    {
        const std::string_view value = Trim(text);
        const bool isTrue = EqualsIgnoringCase(value, "true") || value == "1";
        const bool isFalse = EqualsIgnoringCase(value, "false") || value == "0";
        if (!isTrue && !isFalse)
            return false;
        const bool result = isTrue;
        std::memcpy(base, &result, sizeof(result));
        return true;
    }

    const ScalarRun run = RunOf(field);
    const std::size_t count = run.Count;
    if (count > kMaxScalars)
        return false;

    std::array<std::string_view, kMaxScalars> pieces{};
    if (!SplitScalars(text, count, pieces))
        return false;

    // Parsed in full before a byte is written, so a vector whose last component
    // is nonsense leaves the whole field as it was.
    std::array<double, kMaxScalars> floating{};
    std::array<std::int64_t, kMaxScalars> integral{};
    const bool isFloating = IsFloatingKind(field.Scalar);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (isFloating)
        {
            if (!ParseFloating(pieces[i], floating[i]))
                return false;
            if (field.DisplayDegrees)
                floating[i] /= kRadiansToDegrees;
        }
        else if (!ParseIntegral(pieces[i], integral[i]))
        {
            return false;
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        std::byte* scalar = base + i * run.Stride;
        if (isFloating)
            WriteFloating(scalar, field, floating[i]);
        else
            WriteInteger(scalar, field, integral[i]);
    }
    return true;
}
