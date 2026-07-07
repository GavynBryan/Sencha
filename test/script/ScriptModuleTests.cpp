#include "ScriptTestSupport.h"

#include <cstring>

#include <gtest/gtest.h>

namespace
{
    // A module exercising every section, including optional ones.
    ScriptModule BuildFullModule()
    {
        ScriptAssembler a;
        a.M.SourceHash = 0x1234567890ABCDEFull;
        a.M.MinHostApiVersion = 1;

        const uint16_t half = a.ConstF(0.5);
        (void)a.Import("core.sqrt", "d d");
        (void)a.FieldBind("HookshotState", "target", ScriptScalarKind::Double, 3);
        (void)a.ComponentBind("HookshotState");
        (void)a.TagBind("hookshot.anchor");
        a.M.AssetBinds.push_back({a.Str("cues/hookshot_fire"), static_cast<uint8_t>(ScriptAssetKind::Cue)});
        a.Param("range", ScriptScalarKind::Float, {BitsF(1800.0)});

        ScriptComponentDef component;
        component.Name = a.Str("HookshotState");
        component.Fields.push_back({a.Str("target.x"), static_cast<uint8_t>(ScriptScalarKind::Double), 1, 0, 0});
        component.Fields.push_back({a.Str("pulling"), static_cast<uint8_t>(ScriptScalarKind::Bool), 1, 24, 0});
        a.M.Components.push_back(std::move(component));

        a.BeginFn("start", 12, 9, 0);
        a.ABx(ScriptOp::Ldc, 9, half);
        a.SAx(ScriptOp::Enter, 0);
        a.Abc(ScriptOp::Ret);
        const uint32_t startFn = a.EndFn();
        a.Decl(ScriptDeclKind::Ability, "Hookshot", {"Pulling"}, {{"start", startFn}});

        a.M.HostEnumFixups.push_back({half, a.Str("QueryMask"), a.Str("Hookshot")});

        a.M.HasDebug = true;
        a.M.DebugFiles.push_back(a.Str("scripts/abilities/hookshot.t"));
        a.M.LineTable.push_back({0, 14, 9, 0});
        a.M.DebugSymbols.push_back({startFn, a.Str("owner"), 9, 1});
        return a.M;
    }

    std::span<const std::byte> Bytes(const std::vector<std::byte>& v)
    {
        return std::span<const std::byte>(v);
    }
}

TEST(ScriptModule, WriteParseRoundTrip)
{
    const ScriptModule original = BuildFullModule();
    const std::vector<std::byte> bytes = WriteScriptModule(original);

    const ScriptModuleParseResult parsed = ParseScriptModule(Bytes(bytes));
    ASSERT_TRUE(parsed.Ok) << parsed.Error;
    const ScriptModule& m = parsed.Module;

    EXPECT_EQ(m.SourceHash, original.SourceHash);
    EXPECT_EQ(m.MinHostApiVersion, original.MinHostApiVersion);
    EXPECT_EQ(m.Strings, original.Strings);
    EXPECT_EQ(m.Constants, original.Constants);
    EXPECT_EQ(m.Code, original.Code);
    ASSERT_EQ(m.Functions.size(), original.Functions.size());
    EXPECT_EQ(m.Functions[0].Name, original.Functions[0].Name);
    EXPECT_EQ(m.Functions[0].FrameSlots, original.Functions[0].FrameSlots);
    ASSERT_EQ(m.Components.size(), 1u);
    EXPECT_EQ(m.Components[0].Fields.size(), 2u);
    EXPECT_EQ(m.ComponentBinds, original.ComponentBinds);
    ASSERT_EQ(m.FieldBinds.size(), 1u);
    EXPECT_EQ(m.FieldBinds[0].SlotCount, 3);
    EXPECT_EQ(m.TagBinds, original.TagBinds);
    ASSERT_EQ(m.AssetBinds.size(), 1u);
    EXPECT_EQ(m.AssetBinds[0].Kind, static_cast<uint8_t>(ScriptAssetKind::Cue));
    ASSERT_EQ(m.HostImports.size(), 1u);
    EXPECT_EQ(m.GetString(m.HostImports[0].Name), "core.sqrt");
    ASSERT_EQ(m.Params.size(), 1u);
    EXPECT_EQ(m.Params[0].DefaultRaw[0], BitsF(1800.0));
    ASSERT_EQ(m.Declarations.size(), 1u);
    EXPECT_EQ(m.Declarations[0].Kind, ScriptDeclKind::Ability);
    ASSERT_EQ(m.Declarations[0].States.size(), 1u);
    EXPECT_EQ(m.GetString(m.Declarations[0].States[0]), "Pulling");
    ASSERT_EQ(m.HostEnumFixups.size(), 1u);
    EXPECT_EQ(m.GetString(m.HostEnumFixups[0].EnumName), "QueryMask");
    EXPECT_TRUE(m.HasDebug);
    ASSERT_EQ(m.LineTable.size(), 1u);
    EXPECT_EQ(m.LineTable[0].Line, 14u);
    ASSERT_EQ(m.DebugSymbols.size(), 1u);
    EXPECT_EQ(m.GetString(m.DebugSymbols[0].Name), "owner");

    // A second write is byte-identical: the writer is deterministic.
    EXPECT_EQ(WriteScriptModule(parsed.Module), bytes);
}

TEST(ScriptModule, GoldenHeaderBytes)
{
    ScriptModule module;
    const std::vector<std::byte> bytes = WriteScriptModule(module);
    ASSERT_GE(bytes.size(), 32u);
    // Magic "TBC0".
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[0]), 'T');
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[1]), 'B');
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[2]), 'C');
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[3]), '0');
    // Container version 1, bytecode version 1, little-endian u16s.
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[4]), 1);
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[5]), 0);
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[6]), 1);
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[7]), 0);
    // Deterministic-strict flag set, debug flag clear.
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[10]), 1);

    const ScriptModuleParseResult parsed = ParseScriptModule(Bytes(bytes));
    EXPECT_TRUE(parsed.Ok) << parsed.Error;
}

namespace
{
    void ExpectReject(std::vector<std::byte> bytes, std::string_view expectedFragment)
    {
        const ScriptModuleParseResult parsed = ParseScriptModule(Bytes(bytes));
        EXPECT_FALSE(parsed.Ok);
        EXPECT_NE(parsed.Error.find(expectedFragment), std::string::npos)
            << "error was: " << parsed.Error;
    }
}

TEST(ScriptModule, RejectsTruncatedHeader)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    bytes.resize(16);
    ExpectReject(std::move(bytes), "header");
}

TEST(ScriptModule, RejectsBadMagic)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    bytes[0] = std::byte{'X'};
    ExpectReject(std::move(bytes), "magic");
}

TEST(ScriptModule, RejectsWrongContainerVersion)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    bytes[4] = std::byte{9};
    ExpectReject(std::move(bytes), "container version");
}

TEST(ScriptModule, RejectsWrongBytecodeVersion)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    bytes[6] = std::byte{7};
    ExpectReject(std::move(bytes), "bytecode format version");
}

TEST(ScriptModule, RejectsSectionOutsideFile)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    // First section table entry's offset field (header is 32 bytes; entry
    // layout is kind, offset, size).
    const size_t offsetField = 32 + 4;
    bytes[offsetField + 0] = std::byte{0xFF};
    bytes[offsetField + 1] = std::byte{0xFF};
    bytes[offsetField + 2] = std::byte{0xFF};
    bytes[offsetField + 3] = std::byte{0x7F};
    ExpectReject(std::move(bytes), "outside the file");
}

TEST(ScriptModule, RejectsDuplicateSectionKind)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    // Rewrite the first entry's kind (StringTable) to ConstPool: the table
    // then contains ConstPool twice.
    bytes[32] = std::byte{0x02};
    ExpectReject(std::move(bytes), "duplicate");
}

TEST(ScriptModule, RejectsMissingRequiredSection)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    // Retag the first entry (StringTable) as an unknown strippable kind: it
    // is skipped, and the required-section check fires.
    bytes[32] = std::byte{0x77};
    ExpectReject(std::move(bytes), "missing required");
}

TEST(ScriptModule, RejectsUnknownHashedSectionKind)
{
    std::vector<std::byte> bytes = WriteScriptModule(BuildFullModule());
    bytes[32] = std::byte{0x1F};
    ExpectReject(std::move(bytes), "unknown section kind");
}

TEST(ScriptModule, RejectsBytecodeHashMismatch)
{
    const ScriptModule module = BuildFullModule();
    std::vector<std::byte> bytes = WriteScriptModule(module);
    // Flip a byte in the first section payload (StringTable, hash-covered).
    // Entry 0 of the table sits at byte 32: kind, offset, size.
    uint32_t firstOffset = 0;
    std::memcpy(&firstOffset, bytes.data() + 36, 4);
    bytes[firstOffset] ^= std::byte{0x01};
    ExpectReject(std::move(bytes), "hash mismatch");
}

TEST(ScriptModule, EmptyModuleRoundTrips)
{
    ScriptModule module;
    const std::vector<std::byte> bytes = WriteScriptModule(module);
    const ScriptModuleParseResult parsed = ParseScriptModule(Bytes(bytes));
    ASSERT_TRUE(parsed.Ok) << parsed.Error;
    EXPECT_TRUE(parsed.Module.Functions.empty());
    EXPECT_FALSE(parsed.Module.HasDebug);
}
