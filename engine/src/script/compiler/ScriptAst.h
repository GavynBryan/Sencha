#pragma once

#include "ScriptLexer.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// AST for T. Nodes carry source
// positions for error reporting and the line table. string_views alias the
// source text, which the compiler keeps alive for the whole compile.

struct ScriptTypeRef
{
    std::string_view Name;    // "f32", "Vec3", "HookshotState", element type when array
    bool IsArray = false;
    uint32_t ArrayCount = 0;
    uint32_t Line = 0;
    uint32_t Col = 0;
};

struct ScriptExpr;
using ScriptExprPtr = std::unique_ptr<ScriptExpr>;

struct ScriptExpr
{
    enum class K : uint8_t
    {
        IntLit, FloatLit, BoolLit,
        TagLit, CueLit, PrefabLit, AssetLit,
        Ident,
        Member, // Lhs . Text
        Index,  // Lhs [ Rhs ]
        Call,   // Lhs ( Args )
        Record, // Text { Names[i]: Args[i] }
        Unary,  // Op Lhs
        Binary, // Lhs Op Rhs
    };

    K Kind = K::IntLit;
    uint32_t Line = 0;
    uint32_t Col = 0;
    std::string_view Text;
    ScriptTokKind Op = ScriptTokKind::Eof;
    ScriptExprPtr Lhs;
    ScriptExprPtr Rhs;
    std::vector<ScriptExprPtr> Args;
    std::vector<std::string_view> Names;
};

struct ScriptStmt;
using ScriptStmtPtr = std::unique_ptr<ScriptStmt>;

struct ScriptStmt
{
    enum class K : uint8_t
    {
        Let,      // Name [: Type] = A
        Assign,   // A Op= B
        ExprStmt, // A (must be a call)
        If,       // A cond, Body, ElseBody (ElseBody may hold a single If)
        While,    // A cond, Body
        For,      // Name in A .. B, Body
        Return,   // A optional
        Enter,    // Name
    };

    K Kind = K::Let;
    uint32_t Line = 0;
    uint32_t Col = 0;
    std::string_view Name;
    std::optional<ScriptTypeRef> Type;
    ScriptTokKind Op = ScriptTokKind::Assign;
    ScriptExprPtr A;
    ScriptExprPtr B;
    std::vector<ScriptStmtPtr> Body;
    std::vector<ScriptStmtPtr> ElseBody;
};

struct ScriptAstField
{
    std::string_view Name;
    ScriptTypeRef Type;
    ScriptExprPtr Default; // literal or null
    uint32_t Line = 0;
    uint32_t Col = 0;
};

// A component's authoring attribute. Config components are script read-only
// (the authored attach point); Runtime components are script read/write. Default
// is read/write like Runtime; the serialization purpose split arrives with the
// consumer.
enum class ScriptComponentAttr : uint8_t
{
    Default,
    Config,
    Runtime,
};

struct ScriptAstComponent
{
    std::string_view Name;
    ScriptComponentAttr Attr = ScriptComponentAttr::Default;
    std::vector<ScriptAstField> Fields;
    uint32_t Line = 0;
    uint32_t Col = 0;
};

struct ScriptAstEnum
{
    std::string_view Name;
    std::vector<std::string_view> Members;
    uint32_t Line = 0;
    uint32_t Col = 0;
};

struct ScriptAstConst
{
    std::string_view Name;
    ScriptTypeRef Type;
    ScriptExprPtr Value;
    bool IsParam = false; // param: asset-overridable, const: compile-time only
    uint32_t Line = 0;
    uint32_t Col = 0;
};

struct ScriptAstFnParam
{
    std::string_view Name;
    ScriptTypeRef Type;
};

struct ScriptAstFn
{
    std::string_view State; // empty for stateless callbacks / free functions
    std::string_view Name;
    std::vector<ScriptAstFnParam> Params;
    std::optional<ScriptTypeRef> Return;
    std::vector<ScriptStmtPtr> Body;
    uint32_t Line = 0;
    uint32_t Col = 0;
};

enum class ScriptAstBlockKind : uint8_t
{
    Ability,
    Behavior,
    Trigger,
    Interaction,
    System,
    Observer,
};

struct ScriptAstBlock
{
    ScriptAstBlockKind Kind = ScriptAstBlockKind::Ability;
    std::string_view Name;
    std::vector<std::string_view> States;
    std::vector<ScriptAstConst> Consts; // consts and params
    std::vector<ScriptAstFn> Fns;
    // Components the declaration's subject entity is required to have. The tick
    // runs only on entities that have them all; the typechecker treats the
    // subject as known to have them (no has() guard needed).
    std::vector<std::string_view> Requires;
    // Components the subject entity must NOT have (`without`): the iteration
    // signature excludes them.
    std::vector<std::string_view> Excludes;
    // A `behavior`'s inline config {} / runtime {} field lists. Desugaring turns
    // them into #[config] <B>Config and #[runtime] <B>State components. HasConfig/
    // HasRuntime distinguish an empty block from an absent one.
    bool HasConfig = false;
    bool HasRuntime = false;
    std::vector<ScriptAstField> ConfigFields;
    std::vector<ScriptAstField> RuntimeFields;
    // Components a `system` declares it writes on the subject (the effect
    // signature). A self component-field write must name a component in this
    // set, and the set must be a subset of the iterated (`over`) components.
    std::vector<std::string_view> Writes;
    // Schedule edges (system names): this system runs after all in After and
    // before all in Before. The compiler topo-sorts systems into tick order.
    std::vector<std::string_view> After;
    std::vector<std::string_view> Before;
    uint32_t Line = 0;
    uint32_t Col = 0;
};

// One parsed source file. Imports are resolved by the compiler driver,
// which parses each unit once and merges declarations.
struct ScriptAstUnit
{
    std::string Path;
    std::vector<std::string_view> Imports;
    std::vector<ScriptAstComponent> Components;
    std::vector<ScriptAstEnum> Enums;
    std::vector<ScriptAstConst> Consts;
    std::vector<ScriptAstFn> FreeFns;
    std::vector<ScriptAstBlock> Blocks;
    uint16_t FileIndex = 0; // into the compile's file list, for the line table
};
