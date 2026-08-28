#pragma once

#include <string>
#include <utility>
#include <vector>

//=============================================================================
// Json5Value
//
// The parsed form of authored scene source (.sscene): the same value shapes as
// the engine's strict JsonValue, plus the two things scene source needs that
// runtime JSON must never carry -- comments, and a stable member order that
// round-trips. The engine parser stays strict and separate on purpose: cooked
// and manifest JSON reject what this accepts.
//
// Trivia model: a comment belongs to the value (or object member) that follows
// it, and a comment with nothing after it belongs to its containing value's
// trailing trivia. Comments are stored verbatim, delimiters included; the
// writer re-indents them but never edits their text, which is what makes
// write(parse(write(x))) byte-identical.
//=============================================================================
struct Json5Value
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    using Member = std::pair<std::string, Json5Value>;

    Kind K = Kind::Null;
    bool Boolean = false;
    double Number = 0.0;
    std::string Text;
    std::vector<Json5Value> Elements;
    std::vector<Member> Members;

    // Comments preceding this value -- for an object member, preceding its key.
    std::vector<std::string> LeadingComments;
    // Comments between the last child and the closing bracket (containers only).
    std::vector<std::string> TrailingComments;

    Json5Value() = default;
    explicit Json5Value(bool value) : K(Kind::Bool), Boolean(value) {}
    explicit Json5Value(double value) : K(Kind::Number), Number(value) {}
    explicit Json5Value(std::string value) : K(Kind::String), Text(std::move(value)) {}

    [[nodiscard]] static Json5Value MakeArray() { Json5Value v; v.K = Kind::Array; return v; }
    [[nodiscard]] static Json5Value MakeObject() { Json5Value v; v.K = Kind::Object; return v; }

    [[nodiscard]] bool IsNull() const { return K == Kind::Null; }
    [[nodiscard]] bool IsBool() const { return K == Kind::Bool; }
    [[nodiscard]] bool IsNumber() const { return K == Kind::Number; }
    [[nodiscard]] bool IsString() const { return K == Kind::String; }
    [[nodiscard]] bool IsArray() const { return K == Kind::Array; }
    [[nodiscard]] bool IsObject() const { return K == Kind::Object; }

    [[nodiscard]] const Json5Value* Find(std::string_view key) const
    {
        for (const Member& member : Members)
            if (member.first == key)
                return &member.second;
        return nullptr;
    }

    [[nodiscard]] Json5Value* FindMutable(std::string_view key)
    {
        for (Member& member : Members)
            if (member.first == key)
                return &member.second;
        return nullptr;
    }

    // Value equality, trivia excluded: two documents that mean the same thing
    // compare equal however they were commented. What the round-trip tests
    // assert about semantics.
    [[nodiscard]] bool SameValueAs(const Json5Value& other) const
    {
        if (K != other.K)
            return false;
        switch (K)
        {
        case Kind::Null: return true;
        case Kind::Bool: return Boolean == other.Boolean;
        case Kind::Number: return Number == other.Number;
        case Kind::String: return Text == other.Text;
        case Kind::Array:
            if (Elements.size() != other.Elements.size())
                return false;
            for (std::size_t i = 0; i < Elements.size(); ++i)
                if (!Elements[i].SameValueAs(other.Elements[i]))
                    return false;
            return true;
        case Kind::Object:
            if (Members.size() != other.Members.size())
                return false;
            for (std::size_t i = 0; i < Members.size(); ++i)
                if (Members[i].first != other.Members[i].first
                    || !Members[i].second.SameValueAs(other.Members[i].second))
                    return false;
            return true;
        }
        return false;
    }
};
