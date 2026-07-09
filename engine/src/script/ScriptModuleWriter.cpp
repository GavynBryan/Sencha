#include "script/ScriptModule.h"

#include "ScriptContainerIo.h"
#include "core/hash/ContentHash.h"

namespace
{
    using ScriptContainerIo::Writer;

    struct PendingSection
    {
        uint32_t Kind = 0;
        std::vector<std::byte> Payload;
    };

    [[nodiscard]] std::vector<std::byte> SerializeStringTable(const ScriptModule& module)
    {
        Writer w;
        w.Write<uint32_t>(static_cast<uint32_t>(module.Strings.size()));
        uint32_t offset = 0;
        for (const std::string& s : module.Strings)
        {
            w.Write<uint32_t>(offset);
            offset += static_cast<uint32_t>(s.size());
        }
        w.Write<uint32_t>(offset);
        for (const std::string& s : module.Strings)
        {
            w.WriteBytes(s.data(), s.size());
        }
        return std::move(w.Bytes);
    }

    template <typename T, typename Fn>
    [[nodiscard]] std::vector<std::byte> SerializeCounted(const std::vector<T>& items, Fn&& writeItem)
    {
        Writer w;
        w.Write<uint32_t>(static_cast<uint32_t>(items.size()));
        for (const T& item : items)
        {
            writeItem(w, item);
        }
        return std::move(w.Bytes);
    }
}

std::vector<std::byte> WriteScriptModule(const ScriptModule& module)
{
    std::vector<PendingSection> sections;
    auto add = [&sections](ScriptSectionKind kind, std::vector<std::byte> payload) {
        sections.push_back({static_cast<uint32_t>(kind), std::move(payload)});
    };

    add(ScriptSectionKind::StringTable, SerializeStringTable(module));

    add(ScriptSectionKind::ConstPool, SerializeCounted(module.Constants, [](Writer& w, uint64_t v) {
        w.Write<uint64_t>(v);
    }));

    add(ScriptSectionKind::Code, SerializeCounted(module.Code, [](Writer& w, uint32_t v) {
        w.Write<uint32_t>(v);
    }));

    add(ScriptSectionKind::Functions, SerializeCounted(module.Functions, [](Writer& w, const ScriptFunctionDef& fn) {
        w.Write<uint32_t>(fn.Name);
        w.Write<uint32_t>(fn.CodeOffset);
        w.Write<uint32_t>(fn.WordCount);
        w.Write<uint8_t>(fn.FrameSlots);
        w.Write<uint8_t>(fn.ArgSlots);
        w.Write<uint8_t>(fn.RetSlots);
        w.Write<uint8_t>(fn.Flags);
    }));

    add(ScriptSectionKind::Components, SerializeCounted(module.Components, [](Writer& w, const ScriptComponentDef& c) {
        w.Write<uint32_t>(c.Name);
        w.Write<uint16_t>(static_cast<uint16_t>(c.Fields.size()));
        for (const ScriptComponentField& field : c.Fields)
        {
            w.Write<uint32_t>(field.Name);
            w.Write<uint8_t>(field.Scalar);
            w.Write<uint8_t>(field.ArrayCount);
            w.Write<uint16_t>(field.ByteOffset);
            w.Write<uint64_t>(field.DefaultRaw);
        }
    }));

    add(ScriptSectionKind::BindComponents, SerializeCounted(module.ComponentBinds, [](Writer& w, uint32_t v) {
        w.Write<uint32_t>(v);
    }));

    add(ScriptSectionKind::BindFields, SerializeCounted(module.FieldBinds, [](Writer& w, const ScriptFieldBind& b) {
        w.Write<uint32_t>(b.ComponentName);
        w.Write<uint32_t>(b.FieldPath);
        w.Write<uint8_t>(b.Scalar);
        w.Write<uint8_t>(b.SlotCount);
        w.Write<uint16_t>(0);
    }));

    add(ScriptSectionKind::BindTags, SerializeCounted(module.TagBinds, [](Writer& w, uint32_t v) {
        w.Write<uint32_t>(v);
    }));

    add(ScriptSectionKind::BindAssets, SerializeCounted(module.AssetBinds, [](Writer& w, const ScriptAssetBind& b) {
        w.Write<uint32_t>(b.Path);
        w.Write<uint8_t>(b.Kind);
        w.Write<uint8_t>(0);
        w.Write<uint16_t>(0);
    }));

    add(ScriptSectionKind::HostImports, SerializeCounted(module.HostImports, [](Writer& w, const ScriptHostImport& h) {
        w.Write<uint32_t>(h.Name);
        w.Write<uint32_t>(h.Signature);
        w.Write<uint16_t>(h.MinHostApiVersion);
        w.Write<uint16_t>(0);
    }));

    add(ScriptSectionKind::Params, SerializeCounted(module.Params, [](Writer& w, const ScriptParamDef& p) {
        w.Write<uint32_t>(p.Name);
        w.Write<uint8_t>(p.Scalar);
        w.Write<uint8_t>(p.SlotCount);
        w.Write<uint16_t>(0);
        for (uint8_t s = 0; s < p.SlotCount; ++s)
        {
            w.Write<uint64_t>(s < p.DefaultRaw.size() ? p.DefaultRaw[s] : 0);
        }
    }));

    add(ScriptSectionKind::Declarations, SerializeCounted(module.Declarations, [](Writer& w, const ScriptDeclaration& d) {
        w.Write<uint32_t>(d.Name);
        w.Write<uint8_t>(static_cast<uint8_t>(d.Kind));
        w.Write<uint8_t>(static_cast<uint8_t>(d.States.size()));
        w.Write<uint16_t>(static_cast<uint16_t>(d.Callbacks.size()));
        for (const uint32_t state : d.States)
        {
            w.Write<uint32_t>(state);
        }
        for (const ScriptCallbackDef& callback : d.Callbacks)
        {
            w.Write<uint32_t>(callback.Name);
            w.Write<uint32_t>(callback.FunctionIndex);
        }
        w.Write<uint16_t>(static_cast<uint16_t>(d.RequiredComponents.size()));
        for (const uint32_t required : d.RequiredComponents)
        {
            w.Write<uint32_t>(required);
        }
        w.Write<uint16_t>(static_cast<uint16_t>(d.ExcludedComponents.size()));
        for (const uint32_t excluded : d.ExcludedComponents)
        {
            w.Write<uint32_t>(excluded);
        }
    }));

    if (!module.HostEnumFixups.empty())
    {
        add(ScriptSectionKind::BindHostEnums, SerializeCounted(module.HostEnumFixups, [](Writer& w, const ScriptHostEnumFixup& f) {
            w.Write<uint32_t>(f.ConstIndex);
            w.Write<uint32_t>(f.EnumName);
            w.Write<uint32_t>(f.MemberName);
        }));
    }

    if (module.HasDebug)
    {
        add(ScriptSectionKind::LineTable, SerializeCounted(module.LineTable, [](Writer& w, const ScriptLineEntry& e) {
            w.Write<uint32_t>(e.CodeOffset);
            w.Write<uint32_t>(e.Line);
            w.Write<uint16_t>(e.Col);
            w.Write<uint16_t>(e.File);
        }));
        add(ScriptSectionKind::DebugFiles, SerializeCounted(module.DebugFiles, [](Writer& w, uint32_t v) {
            w.Write<uint32_t>(v);
        }));
        add(ScriptSectionKind::DebugSymbols, SerializeCounted(module.DebugSymbols, [](Writer& w, const ScriptDebugLocal& l) {
            w.Write<uint32_t>(l.FunctionIndex);
            w.Write<uint32_t>(l.Name);
            w.Write<uint8_t>(l.Slot);
            w.Write<uint8_t>(l.SlotCount);
            w.Write<uint16_t>(0);
        }));
    }

    // Hash before layout: it covers non-strippable payloads in table order,
    // which is the emission order above.
    std::vector<std::byte> hashed;
    for (const PendingSection& section : sections)
    {
        if (section.Kind < kScriptFirstStrippableSectionKind)
        {
            hashed.insert(hashed.end(), section.Payload.begin(), section.Payload.end());
        }
    }
    const uint64_t bytecodeHash = HashBytes64(std::span<const std::byte>(hashed));

    const size_t headerSize = 32;
    const size_t tableSize = sections.size() * 12;
    size_t cursor = headerSize + tableSize;
    std::vector<uint32_t> offsets;
    offsets.reserve(sections.size());
    for (const PendingSection& section : sections)
    {
        cursor = (cursor + 7) & ~size_t{7};
        offsets.push_back(static_cast<uint32_t>(cursor));
        cursor += section.Payload.size();
    }

    uint16_t flags = module.Flags;
    if (module.HasDebug)
    {
        flags |= kScriptFlagHasDebug;
    }
    else
    {
        flags &= static_cast<uint16_t>(~kScriptFlagHasDebug);
    }

    Writer out;
    out.Bytes.reserve(cursor);
    out.Write<uint32_t>(kScriptContainerMagic);
    out.Write<uint16_t>(kScriptContainerVersion);
    out.Write<uint16_t>(kScriptBytecodeVersion);
    out.Write<uint16_t>(module.MinHostApiVersion);
    out.Write<uint16_t>(flags);
    out.Write<uint32_t>(static_cast<uint32_t>(sections.size()));
    out.Write<uint64_t>(module.SourceHash);
    out.Write<uint64_t>(bytecodeHash);

    for (size_t i = 0; i < sections.size(); ++i)
    {
        out.Write<uint32_t>(sections[i].Kind);
        out.Write<uint32_t>(offsets[i]);
        out.Write<uint32_t>(static_cast<uint32_t>(sections[i].Payload.size()));
    }

    for (size_t i = 0; i < sections.size(); ++i)
    {
        while (out.Bytes.size() < offsets[i])
        {
            out.Write<uint8_t>(0);
        }
        out.WriteBytes(sections[i].Payload.data(), sections[i].Payload.size());
    }

    return std::move(out.Bytes);
}
