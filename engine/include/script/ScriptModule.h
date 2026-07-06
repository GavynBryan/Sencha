#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// ScriptModule (docs/plans/t-language-v1-spec.md, section B)
//
// The in-memory form of a cooked T script and the .tbc parser/writer. The
// container is little-endian: a 32-byte header, a section table, then 8-byte
// aligned section payloads. Nothing runtime-fragile is baked in: tags,
// component fields, assets, host imports, and host enums are symbolic here
// and resolve into runtime bind tables at module link (a later milestone).
//
// This layer depends only on core/hash. Parsing rejects malformed containers;
// instruction-level checking is ScriptBytecodeValidator's job.
//=============================================================================

inline constexpr uint32_t kScriptContainerMagic = 0x30434254u; // "TBC0" little-endian
inline constexpr uint16_t kScriptContainerVersion = 1;
inline constexpr uint16_t kScriptBytecodeVersion = 0;

inline constexpr uint16_t kScriptFlagDeterministicStrict = 1u << 0;
inline constexpr uint16_t kScriptFlagHasDebug            = 1u << 1;

enum class ScriptSectionKind : uint32_t
{
    StringTable    = 0x01,
    ConstPool      = 0x02,
    Code           = 0x03,
    Functions      = 0x04,
    Components     = 0x05,
    BindComponents = 0x06,
    BindFields     = 0x07,
    BindTags       = 0x08,
    BindAssets     = 0x09,
    HostImports    = 0x0A,
    Params         = 0x0B,
    Declarations   = 0x0C,
    BindHostEnums  = 0x0D,
    LineTable      = 0x40,
    DebugFiles     = 0x41,
    DebugSymbols   = 0x42,
};

// Kinds at or above this value are strippable debug data and are excluded
// from the bytecode hash.
inline constexpr uint32_t kScriptFirstStrippableSectionKind = 0x40;

// Scalar kinds follow FieldScalar order (core/metadata/RuntimeSchema.h) so a
// later milestone can map them without a translation table; 16+ are
// script-only extensions from spec section B.
enum class ScriptScalarKind : uint8_t
{
    Bool   = 0,
    Int32  = 1,
    UInt32 = 2,
    Float  = 3,
    Double = 4,
    Color3 = 5,
    Entity = 16,
    TagId  = 17,
    Int64  = 18,
};

// Storage size in bytes of one scalar of the given kind (Color3 is three
// contiguous floats; Entity is a generational id pair).
[[nodiscard]] constexpr size_t ScriptScalarSize(uint8_t scalar)
{
    switch (static_cast<ScriptScalarKind>(scalar))
    {
    case ScriptScalarKind::Bool:   return 1;
    case ScriptScalarKind::Int32:  return 4;
    case ScriptScalarKind::UInt32: return 4;
    case ScriptScalarKind::Float:  return 4;
    case ScriptScalarKind::Double: return 8;
    case ScriptScalarKind::Color3: return 12;
    case ScriptScalarKind::Entity: return 8;
    case ScriptScalarKind::TagId:  return 4;
    case ScriptScalarKind::Int64:  return 8;
    }
    return 4;
}

enum class ScriptDeclKind : uint8_t
{
    Ability     = 1,
    Behavior    = 2,
    Trigger     = 3,
    Interaction = 4,
};

enum class ScriptAssetKind : uint8_t
{
    Cue     = 1,
    Prefab  = 2,
    Generic = 3,
};

inline constexpr uint8_t kScriptFunctionFlagStateCallback = 1u << 0;

struct ScriptFunctionDef
{
    uint32_t Name = 0;       // string index
    uint32_t CodeOffset = 0; // word index into Code
    uint32_t WordCount = 0;
    uint8_t FrameSlots = 0;
    uint8_t ArgSlots = 0;
    uint8_t RetSlots = 0;
    uint8_t Flags = 0;
};

struct ScriptComponentField
{
    uint32_t Name = 0; // string index, dotted leaf
    uint8_t Scalar = 0;
    uint8_t ArrayCount = 1;
    uint16_t ByteOffset = 0;
    uint64_t DefaultRaw = 0;
};

struct ScriptComponentDef
{
    uint32_t Name = 0; // string index, unprefixed
    std::vector<ScriptComponentField> Fields;
};

struct ScriptFieldBind
{
    uint32_t ComponentName = 0; // string index
    uint32_t FieldPath = 0;     // string index, dotted
    uint8_t Scalar = 0;
    uint8_t SlotCount = 0;
};

struct ScriptAssetBind
{
    uint32_t Path = 0; // string index
    uint8_t Kind = 0;  // ScriptAssetKind
};

struct ScriptHostImport
{
    uint32_t Name = 0;      // string index, dotted
    uint32_t Signature = 0; // string index, spec section C encoding
    uint16_t MinHostApiVersion = 0;
};

struct ScriptParamDef
{
    uint32_t Name = 0; // string index
    uint8_t Scalar = 0;
    uint8_t SlotCount = 1;
    std::vector<uint64_t> DefaultRaw; // SlotCount entries
};

struct ScriptCallbackDef
{
    uint32_t Name = 0; // string index: "start", "Pulling.fixed", ...
    uint32_t FunctionIndex = 0;
};

struct ScriptDeclaration
{
    uint32_t Name = 0; // string index
    ScriptDeclKind Kind = ScriptDeclKind::Ability;
    std::vector<uint32_t> States; // string indices, declaration order
    std::vector<ScriptCallbackDef> Callbacks;
};

struct ScriptHostEnumFixup
{
    uint32_t ConstIndex = 0;
    uint32_t EnumName = 0;   // string index
    uint32_t MemberName = 0; // string index
};

struct ScriptLineEntry
{
    uint32_t CodeOffset = 0; // word index
    uint32_t Line = 0;
    uint16_t Col = 0;
    uint16_t File = 0; // index into DebugFiles
};

struct ScriptDebugLocal
{
    uint32_t FunctionIndex = 0;
    uint32_t Name = 0; // string index
    uint8_t Slot = 0;
    uint8_t SlotCount = 0;
};

struct ScriptModule
{
    uint16_t MinHostApiVersion = 1;
    uint16_t Flags = kScriptFlagDeterministicStrict;
    uint64_t SourceHash = 0;
    uint64_t BytecodeHash = 0; // recomputed on write; verified on parse

    std::vector<std::string> Strings;
    std::vector<uint64_t> Constants;
    std::vector<uint32_t> Code;
    std::vector<ScriptFunctionDef> Functions;
    std::vector<ScriptComponentDef> Components;
    std::vector<uint32_t> ComponentBinds; // string indices
    std::vector<ScriptFieldBind> FieldBinds;
    std::vector<uint32_t> TagBinds; // string indices
    std::vector<ScriptAssetBind> AssetBinds;
    std::vector<ScriptHostImport> HostImports;
    std::vector<ScriptParamDef> Params;
    std::vector<ScriptDeclaration> Declarations;
    std::vector<ScriptHostEnumFixup> HostEnumFixups;

    bool HasDebug = false;
    std::vector<ScriptLineEntry> LineTable;
    std::vector<uint32_t> DebugFiles; // string indices
    std::vector<ScriptDebugLocal> DebugSymbols;

    [[nodiscard]] std::string_view GetString(uint32_t index) const
    {
        return index < Strings.size() ? std::string_view(Strings[index]) : std::string_view();
    }
};

struct ScriptModuleParseResult
{
    bool Ok = false;
    std::string Error;
    ScriptModule Module;
};

// Parses and structurally verifies a .tbc image. Rejects: bad magic, wrong
// container/bytecode versions, section table entries outside the file,
// duplicate or missing required sections, truncated payloads, out-of-range
// string indices, and a bytecode hash mismatch.
[[nodiscard]] ScriptModuleParseResult ParseScriptModule(std::span<const std::byte> bytes);

// Serializes a module to .tbc bytes: sections in kind order, 8-byte aligned
// payloads, header hashes recomputed (Module.BytecodeHash is ignored on
// input; SourceHash passes through). Debug sections are emitted only when
// HasDebug is set.
[[nodiscard]] std::vector<std::byte> WriteScriptModule(const ScriptModule& module);

// Hash over the declared component schemas (names, leaf paths, scalar kinds,
// offsets, array counts). Equal hashes mean a hot reload can swap the module
// in place; a change routes to the world-reload path (spec answer 17).
[[nodiscard]] uint64_t ComputeScriptComponentSchemaHash(const ScriptModule& module);
