#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Lexer for T. Produces a token stream with explicit Terminator
// tokens: a newline terminates a statement unless paren/bracket depth is
// open, the previous token is a continuation suffix (binary operator, comma,
// assignment, dot, arrow, opening bracket), or the next line begins with a
// continuation prefix (dot or binary operator). ';' is an explicit
// terminator; runs collapse.

enum class ScriptTokKind : uint8_t
{
    Eof,
    Terminator,
    Ident,
    IntLit,
    FloatLit,
    StringLit,   // "...": only valid after tag/cue/prefab/asset/import
    TagLit,      // tag"..."
    CueLit,      // cue"..."
    PrefabLit,   // prefab"..."
    AssetLit,    // asset"..."
    // Keywords.
    KwComponent, KwEnum, KwAbility, KwBehavior, KwTrigger, KwInteraction,
    KwState, KwFn, KwConst, KwParam, KwImport, KwLet, KwIf, KwElse, KwWhile,
    KwFor, KwIn, KwReturn, KwEnter, KwTrue, KwFalse,
    // Punctuation and operators.
    LBrace, RBrace, LParen, RParen, LBracket, RBracket,
    Comma, Dot, Colon, Arrow, DotDot,
    Plus, Minus, Star, Slash, Percent,
    Not, AndAnd, OrOr,
    EqEq, NotEq, Lt, Le, Gt, Ge,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
};

struct ScriptToken
{
    ScriptTokKind Kind = ScriptTokKind::Eof;
    std::string_view Text; // slice of the source for Ident/literals
    uint32_t Line = 0;     // 1-based
    uint32_t Col = 0;      // 1-based
};

struct ScriptLexResult
{
    bool Ok = false;
    std::string Error;
    uint32_t ErrorLine = 0;
    uint32_t ErrorCol = 0;
    std::vector<ScriptToken> Tokens; // ends with Eof
};

[[nodiscard]] ScriptLexResult LexScript(std::string_view source);
