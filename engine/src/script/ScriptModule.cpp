#include "script/ScriptModule.h"

#include "ScriptContainerIo.h"
#include "script/ScriptBytecode.h"
#include "core/hash/ContentHash.h"

#include <algorithm>
#include <optional>

namespace
{
    using ScriptContainerIo::Reader;

    constexpr size_t kHeaderSize = 32;
    constexpr size_t kSectionTableEntrySize = 12;
    // Backstop against a hostile section count allocating the world before
    // per-entry bounds checks run. Far above any real module.
    constexpr uint32_t kMaxSections = 64;

    struct SectionEntry
    {
        uint32_t Kind = 0;
        uint32_t Offset = 0;
        uint32_t Size = 0;
    };

    struct ParseCursor
    {
        ScriptModuleParseResult& Result;

        bool Fail(std::string message)
        {
            Result.Ok = false;
            Result.Error = std::move(message);
            return false;
        }
    };

    [[nodiscard]] bool IsKnownSectionKind(uint32_t kind)
    {
        switch (static_cast<ScriptSectionKind>(kind))
        {
        case ScriptSectionKind::StringTable:
        case ScriptSectionKind::ConstPool:
        case ScriptSectionKind::Code:
        case ScriptSectionKind::Functions:
        case ScriptSectionKind::Components:
        case ScriptSectionKind::BindComponents:
        case ScriptSectionKind::BindFields:
        case ScriptSectionKind::BindTags:
        case ScriptSectionKind::BindAssets:
        case ScriptSectionKind::HostImports:
        case ScriptSectionKind::Params:
        case ScriptSectionKind::Declarations:
        case ScriptSectionKind::BindHostEnums:
        case ScriptSectionKind::LineTable:
        case ScriptSectionKind::DebugFiles:
        case ScriptSectionKind::DebugSymbols:
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ParseStringTable(std::span<const std::byte> payload, ScriptModule& module,
                                        std::string& error)
    {
        Reader reader{payload};
        const uint32_t count = reader.Read<uint32_t>();
        std::vector<uint32_t> offsets;
        offsets.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            offsets.push_back(reader.Read<uint32_t>());
        }
        const uint32_t blobSize = reader.Read<uint32_t>();
        if (reader.Failed || reader.Offset + blobSize > payload.size())
        {
            error = "StringTable section truncated";
            return false;
        }
        const char* blob = reinterpret_cast<const char*>(payload.data() + reader.Offset);
        module.Strings.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t begin = offsets[i];
            const uint32_t end = (i + 1 < count) ? offsets[i + 1] : blobSize;
            if (begin > end || end > blobSize)
            {
                error = "StringTable offsets are not monotonic within the blob";
                return false;
            }
            module.Strings.emplace_back(blob + begin, end - begin);
        }
        return true;
    }

    [[nodiscard]] bool CheckString(const ScriptModule& module, uint32_t index,
                                   const char* where, std::string& error)
    {
        if (index >= module.Strings.size())
        {
            error = std::string("string index out of range in ") + where;
            return false;
        }
        return true;
    }
}

ScriptModuleParseResult ParseScriptModule(std::span<const std::byte> bytes)
{
    ScriptModuleParseResult result;
    ScriptModule& module = result.Module;
    auto fail = [&result](std::string message) -> ScriptModuleParseResult& {
        result.Ok = false;
        result.Error = std::move(message);
        return result;
    };

    Reader header{bytes};
    const uint32_t magic = header.Read<uint32_t>();
    const uint16_t containerVersion = header.Read<uint16_t>();
    const uint16_t bytecodeVersion = header.Read<uint16_t>();
    module.MinHostApiVersion = header.Read<uint16_t>();
    module.Flags = header.Read<uint16_t>();
    const uint32_t sectionCount = header.Read<uint32_t>();
    module.SourceHash = header.Read<uint64_t>();
    module.BytecodeHash = header.Read<uint64_t>();
    if (header.Failed)
    {
        return fail("file smaller than the 32-byte header");
    }
    if (magic != kScriptContainerMagic)
    {
        return fail("bad magic (not a .tbc file)");
    }
    if (containerVersion != kScriptContainerVersion)
    {
        return fail("unsupported container version");
    }
    if (bytecodeVersion != kScriptBytecodeVersion)
    {
        return fail("unsupported bytecode format version");
    }
    if (sectionCount > kMaxSections)
    {
        return fail("section count exceeds the container limit");
    }

    std::vector<SectionEntry> sections;
    sections.reserve(sectionCount);
    uint64_t seenKinds[2] = {0, 0}; // bitset over kind values 0-127
    for (uint32_t i = 0; i < sectionCount; ++i)
    {
        SectionEntry entry;
        entry.Kind = header.Read<uint32_t>();
        entry.Offset = header.Read<uint32_t>();
        entry.Size = header.Read<uint32_t>();
        if (header.Failed)
        {
            return fail("section table truncated");
        }
        if (entry.Offset < kHeaderSize + sectionCount * kSectionTableEntrySize
            || static_cast<uint64_t>(entry.Offset) + entry.Size > bytes.size())
        {
            return fail("section payload outside the file");
        }
        if (entry.Kind >= 128 || !IsKnownSectionKind(entry.Kind))
        {
            // Unknown strippable kinds pass through (future debug data);
            // unknown hashed kinds would change hash semantics, so reject.
            if (entry.Kind < kScriptFirstStrippableSectionKind)
            {
                return fail("unknown section kind below the strippable range");
            }
            continue;
        }
        const uint32_t word = entry.Kind / 64;
        const uint64_t bit = 1ull << (entry.Kind % 64);
        if (seenKinds[word] & bit)
        {
            return fail("duplicate section kind");
        }
        seenKinds[word] |= bit;
        sections.push_back(entry);
    }

    for (uint32_t required = static_cast<uint32_t>(ScriptSectionKind::StringTable);
         required <= static_cast<uint32_t>(ScriptSectionKind::Declarations); ++required)
    {
        if (!(seenKinds[required / 64] & (1ull << (required % 64))))
        {
            return fail("missing required section");
        }
    }

    // Hash covers non-strippable payloads concatenated in section-table order.
    {
        std::vector<std::byte> hashed;
        for (const SectionEntry& entry : sections)
        {
            if (entry.Kind >= kScriptFirstStrippableSectionKind)
            {
                continue;
            }
            const auto payload = bytes.subspan(entry.Offset, entry.Size);
            hashed.insert(hashed.end(), payload.begin(), payload.end());
        }
        if (HashBytes64(std::span<const std::byte>(hashed)) != module.BytecodeHash)
        {
            return fail("bytecode hash mismatch");
        }
    }

    auto payloadOf = [&bytes, &sections](ScriptSectionKind kind) -> std::optional<std::span<const std::byte>> {
        for (const SectionEntry& entry : sections)
        {
            if (entry.Kind == static_cast<uint32_t>(kind))
            {
                return bytes.subspan(entry.Offset, entry.Size);
            }
        }
        return std::nullopt;
    };

    std::string error;
    if (!ParseStringTable(*payloadOf(ScriptSectionKind::StringTable), module, error))
    {
        return fail(std::move(error));
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::ConstPool)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            module.Constants.push_back(r.Read<uint64_t>());
        }
        if (r.Failed || module.Constants.size() > kScriptMaxConstants)
        {
            return fail("ConstPool section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::Code)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            module.Code.push_back(r.Read<uint32_t>());
        }
        if (r.Failed)
        {
            return fail("Code section truncated");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::Functions)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptFunctionDef fn;
            fn.Name = r.Read<uint32_t>();
            fn.CodeOffset = r.Read<uint32_t>();
            fn.WordCount = r.Read<uint32_t>();
            fn.FrameSlots = r.Read<uint8_t>();
            fn.ArgSlots = r.Read<uint8_t>();
            fn.RetSlots = r.Read<uint8_t>();
            fn.Flags = r.Read<uint8_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, fn.Name, "Functions", error))
                {
                    return fail(std::move(error));
                }
                module.Functions.push_back(fn);
            }
        }
        if (r.Failed || module.Functions.size() > kScriptMaxFunctions)
        {
            return fail("Functions section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::Components)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptComponentDef component;
            component.Name = r.Read<uint32_t>();
            const uint16_t fieldCount = r.Read<uint16_t>();
            for (uint16_t f = 0; f < fieldCount && !r.Failed; ++f)
            {
                ScriptComponentField field;
                field.Name = r.Read<uint32_t>();
                field.Scalar = r.Read<uint8_t>();
                field.ArrayCount = r.Read<uint8_t>();
                field.ByteOffset = r.Read<uint16_t>();
                field.DefaultRaw = r.Read<uint64_t>();
                if (!r.Failed)
                {
                    if (!CheckString(module, field.Name, "Components", error))
                    {
                        return fail(std::move(error));
                    }
                    component.Fields.push_back(field);
                }
            }
            if (!r.Failed)
            {
                if (!CheckString(module, component.Name, "Components", error))
                {
                    return fail(std::move(error));
                }
                module.Components.push_back(std::move(component));
            }
        }
        if (r.Failed)
        {
            return fail("Components section truncated");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::BindComponents)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            const uint32_t name = r.Read<uint32_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, name, "BindComponents", error))
                {
                    return fail(std::move(error));
                }
                module.ComponentBinds.push_back(name);
            }
        }
        if (r.Failed || module.ComponentBinds.size() > kScriptMaxComponentBinds)
        {
            return fail("BindComponents section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::BindFields)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptFieldBind bind;
            bind.ComponentName = r.Read<uint32_t>();
            bind.FieldPath = r.Read<uint32_t>();
            bind.Scalar = r.Read<uint8_t>();
            bind.SlotCount = r.Read<uint8_t>();
            (void)r.Read<uint16_t>(); // reserved
            if (!r.Failed)
            {
                if (!CheckString(module, bind.ComponentName, "BindFields", error)
                    || !CheckString(module, bind.FieldPath, "BindFields", error))
                {
                    return fail(std::move(error));
                }
                module.FieldBinds.push_back(bind);
            }
        }
        if (r.Failed)
        {
            return fail("BindFields section truncated");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::BindTags)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            const uint32_t name = r.Read<uint32_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, name, "BindTags", error))
                {
                    return fail(std::move(error));
                }
                module.TagBinds.push_back(name);
            }
        }
        if (r.Failed || module.TagBinds.size() > kScriptMaxTagBinds)
        {
            return fail("BindTags section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::BindAssets)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptAssetBind bind;
            bind.Path = r.Read<uint32_t>();
            bind.Kind = r.Read<uint8_t>();
            (void)r.Read<uint8_t>();
            (void)r.Read<uint16_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, bind.Path, "BindAssets", error))
                {
                    return fail(std::move(error));
                }
                if (bind.Kind < 1 || bind.Kind > 3)
                {
                    return fail("BindAssets entry has an invalid asset kind");
                }
                module.AssetBinds.push_back(bind);
            }
        }
        if (r.Failed)
        {
            return fail("BindAssets section truncated");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::HostImports)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptHostImport import;
            import.Name = r.Read<uint32_t>();
            import.Signature = r.Read<uint32_t>();
            import.MinHostApiVersion = r.Read<uint16_t>();
            (void)r.Read<uint16_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, import.Name, "HostImports", error)
                    || !CheckString(module, import.Signature, "HostImports", error))
                {
                    return fail(std::move(error));
                }
                module.HostImports.push_back(import);
            }
        }
        if (r.Failed || module.HostImports.size() > kScriptMaxHostImports)
        {
            return fail("HostImports section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::Params)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptParamDef param;
            param.Name = r.Read<uint32_t>();
            param.Scalar = r.Read<uint8_t>();
            param.SlotCount = r.Read<uint8_t>();
            (void)r.Read<uint16_t>();
            for (uint8_t s = 0; s < param.SlotCount && !r.Failed; ++s)
            {
                param.DefaultRaw.push_back(r.Read<uint64_t>());
            }
            if (!r.Failed)
            {
                if (!CheckString(module, param.Name, "Params", error))
                {
                    return fail(std::move(error));
                }
                module.Params.push_back(std::move(param));
            }
        }
        if (r.Failed || module.Params.size() > kScriptMaxParams)
        {
            return fail("Params section truncated or over limit");
        }
    }

    {
        Reader r{*payloadOf(ScriptSectionKind::Declarations)};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptDeclaration decl;
            decl.Name = r.Read<uint32_t>();
            const uint8_t kind = r.Read<uint8_t>();
            const uint8_t stateCount = r.Read<uint8_t>();
            const uint16_t callbackCount = r.Read<uint16_t>();
            if (kind < 1 || kind > 4)
            {
                return fail("Declarations entry has an invalid kind");
            }
            decl.Kind = static_cast<ScriptDeclKind>(kind);
            for (uint8_t s = 0; s < stateCount && !r.Failed; ++s)
            {
                decl.States.push_back(r.Read<uint32_t>());
            }
            for (uint16_t c = 0; c < callbackCount && !r.Failed; ++c)
            {
                ScriptCallbackDef callback;
                callback.Name = r.Read<uint32_t>();
                callback.FunctionIndex = r.Read<uint32_t>();
                decl.Callbacks.push_back(callback);
            }
            const uint16_t requiredCount = r.Read<uint16_t>();
            for (uint16_t q = 0; q < requiredCount && !r.Failed; ++q)
            {
                decl.RequiredComponents.push_back(r.Read<uint32_t>());
            }
            if (!r.Failed)
            {
                if (!CheckString(module, decl.Name, "Declarations", error))
                {
                    return fail(std::move(error));
                }
                for (const uint32_t state : decl.States)
                {
                    if (!CheckString(module, state, "Declarations", error))
                    {
                        return fail(std::move(error));
                    }
                }
                for (const ScriptCallbackDef& callback : decl.Callbacks)
                {
                    if (!CheckString(module, callback.Name, "Declarations", error))
                    {
                        return fail(std::move(error));
                    }
                    if (callback.FunctionIndex >= module.Functions.size())
                    {
                        return fail("Declarations callback references a missing function");
                    }
                }
                for (const uint32_t required : decl.RequiredComponents)
                {
                    if (!CheckString(module, required, "Declarations", error))
                    {
                        return fail(std::move(error));
                    }
                }
                module.Declarations.push_back(std::move(decl));
            }
        }
        if (r.Failed)
        {
            return fail("Declarations section truncated");
        }
    }

    if (auto payload = payloadOf(ScriptSectionKind::BindHostEnums))
    {
        Reader r{*payload};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptHostEnumFixup fixup;
            fixup.ConstIndex = r.Read<uint32_t>();
            fixup.EnumName = r.Read<uint32_t>();
            fixup.MemberName = r.Read<uint32_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, fixup.EnumName, "BindHostEnums", error)
                    || !CheckString(module, fixup.MemberName, "BindHostEnums", error))
                {
                    return fail(std::move(error));
                }
                if (fixup.ConstIndex >= module.Constants.size())
                {
                    return fail("BindHostEnums fixup references a missing constant");
                }
                module.HostEnumFixups.push_back(fixup);
            }
        }
        if (r.Failed)
        {
            return fail("BindHostEnums section truncated");
        }
    }

    if (auto payload = payloadOf(ScriptSectionKind::LineTable))
    {
        module.HasDebug = true;
        Reader r{*payload};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptLineEntry entry;
            entry.CodeOffset = r.Read<uint32_t>();
            entry.Line = r.Read<uint32_t>();
            entry.Col = r.Read<uint16_t>();
            entry.File = r.Read<uint16_t>();
            if (!r.Failed)
            {
                module.LineTable.push_back(entry);
            }
        }
        if (r.Failed)
        {
            return fail("LineTable section truncated");
        }
    }

    if (auto payload = payloadOf(ScriptSectionKind::DebugFiles))
    {
        module.HasDebug = true;
        Reader r{*payload};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            const uint32_t path = r.Read<uint32_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, path, "DebugFiles", error))
                {
                    return fail(std::move(error));
                }
                module.DebugFiles.push_back(path);
            }
        }
        if (r.Failed)
        {
            return fail("DebugFiles section truncated");
        }
    }

    if (auto payload = payloadOf(ScriptSectionKind::DebugSymbols))
    {
        module.HasDebug = true;
        Reader r{*payload};
        const uint32_t count = r.Read<uint32_t>();
        for (uint32_t i = 0; i < count && !r.Failed; ++i)
        {
            ScriptDebugLocal local;
            local.FunctionIndex = r.Read<uint32_t>();
            local.Name = r.Read<uint32_t>();
            local.Slot = r.Read<uint8_t>();
            local.SlotCount = r.Read<uint8_t>();
            (void)r.Read<uint16_t>();
            if (!r.Failed)
            {
                if (!CheckString(module, local.Name, "DebugSymbols", error))
                {
                    return fail(std::move(error));
                }
                module.DebugSymbols.push_back(local);
            }
        }
        if (r.Failed)
        {
            return fail("DebugSymbols section truncated");
        }
    }

    result.Ok = true;
    return result;
}

uint64_t ComputeScriptComponentSchemaHash(const ScriptModule& module)
{
    // Cover the fields that decide storage layout and reflection identity:
    // component name, each leaf path, scalar kind, array count, byte offset.
    // Defaults and bytecode are deliberately excluded so a logic-only edit
    // keeps the same hash and hot-swaps in place.
    ScriptContainerIo::Writer w;
    for (const ScriptComponentDef& component : module.Components)
    {
        const std::string_view name = module.GetString(component.Name);
        w.WriteBytes(name.data(), name.size());
        w.Write<uint8_t>(0);
        for (const ScriptComponentField& field : component.Fields)
        {
            const std::string_view path = module.GetString(field.Name);
            w.WriteBytes(path.data(), path.size());
            w.Write<uint8_t>(0);
            w.Write<uint8_t>(field.Scalar);
            w.Write<uint8_t>(field.ArrayCount);
            w.Write<uint16_t>(field.ByteOffset);
        }
    }
    return w.Bytes.empty() ? 0 : HashBytes64(std::span<const std::byte>(w.Bytes));
}

ScriptComponentLayout ComputeScriptComponentLayout(const ScriptComponentDef& component)
{
    ScriptComponentLayout layout;
    uint16_t end = 0;
    for (const ScriptComponentField& field : component.Fields)
    {
        const size_t scalarSize = ScriptScalarSize(field.Scalar);
        const uint16_t leafEnd =
            static_cast<uint16_t>(field.ByteOffset + scalarSize * std::max<uint8_t>(field.ArrayCount, 1));
        end = std::max(end, leafEnd);
        layout.Alignment = std::max(layout.Alignment, scalarSize);
    }
    layout.Size = (end + layout.Alignment - 1) & ~(layout.Alignment - 1);
    return layout;
}
