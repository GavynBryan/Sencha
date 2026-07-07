#include <script/ScriptComponentSerializer.h>

#include <core/hash/ContentHash.h>
#include <core/serialization/Archive.h>
#include <ecs/ComponentId.h>
#include <ecs/World.h>
#include <script/ScriptModule.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

// Declared in <world/serialization/SceneSerializer.h>; forward-declared here to
// keep script code clear of the render/audio-pulling scene-codec headers, the
// same pattern the framework's hand-written serializers use.
void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);

namespace
{
    // A field round-trips through the inspector and scene archive only when it is
    // one scalar of a kind the text archive can carry (bool/float/double/u32).
    // 64-bit and reference-shaped kinds and fixed arrays are shown read-only and
    // keep their declared defaults.
    bool IsSupportedScalar(const ScriptComponentField& field)
    {
        if (field.ArrayCount != 1)
            return false;
        switch (static_cast<ScriptScalarKind>(field.Scalar))
        {
        case ScriptScalarKind::Bool:
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        case ScriptScalarKind::Float:
        case ScriptScalarKind::Double:
            return true;
        default:
            return false;
        }
    }

    FieldScalar ToFieldScalar(ScriptScalarKind kind)
    {
        switch (kind)
        {
        case ScriptScalarKind::Bool:   return FieldScalar::Bool;
        case ScriptScalarKind::Int32:  return FieldScalar::Int32;
        case ScriptScalarKind::UInt32: return FieldScalar::UInt32;
        case ScriptScalarKind::Float:  return FieldScalar::Float;
        case ScriptScalarKind::Double: return FieldScalar::Double;
        default:                        return FieldScalar::Unsupported;
        }
    }

    // Binary chunk id for a component whose identity is only known at runtime.
    // The editor and runtime persist scenes through the JSON path (keyed by
    // JsonKey), so this is consulted only for binary scenes; a name hash keeps it
    // stable per component with negligible collision risk.
    std::uint32_t DeriveChunkId(std::string_view key)
    {
        const std::uint64_t h = HashBytes64(key);
        return static_cast<std::uint32_t>(h ^ (h >> 32));
    }

    void WriteLeaf(IWriteArchive& archive, std::string_view name, const std::byte* at,
                   ScriptScalarKind scalar)
    {
        switch (scalar)
        {
        case ScriptScalarKind::Bool:
        {
            std::uint8_t v = 0;
            std::memcpy(&v, at, 1);
            archive.Field(name, v != 0);
            break;
        }
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        {
            std::uint32_t v = 0;
            std::memcpy(&v, at, 4);
            archive.Field(name, v);
            break;
        }
        case ScriptScalarKind::Float:
        {
            float v = 0.0f;
            std::memcpy(&v, at, 4);
            archive.Field(name, v);
            break;
        }
        case ScriptScalarKind::Double:
        {
            double v = 0.0;
            std::memcpy(&v, at, 8);
            archive.Field(name, v);
            break;
        }
        default:
            break; // unsupported leaves are not emitted
        }
    }

    // Reads one leaf into the component image. A missing key (only possible in a
    // text archive; binary always reports present) leaves the default in place,
    // which is how an older scene missing a newly added field migrates.
    void ReadLeaf(IReadArchive& archive, std::string_view name, std::byte* at,
                  ScriptScalarKind scalar)
    {
        if (!archive.HasField(name))
            return;
        switch (scalar)
        {
        case ScriptScalarKind::Bool:
        {
            bool v = false;
            archive.Field(name, v);
            const std::uint8_t b = v ? 1u : 0u;
            std::memcpy(at, &b, 1);
            break;
        }
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        {
            std::uint32_t v = 0;
            archive.Field(name, v);
            std::memcpy(at, &v, 4);
            break;
        }
        case ScriptScalarKind::Float:
        {
            float v = 0.0f;
            archive.Field(name, v);
            std::memcpy(at, &v, 4);
            break;
        }
        case ScriptScalarKind::Double:
        {
            double v = 0.0;
            archive.Field(name, v);
            std::memcpy(at, &v, 8);
            break;
        }
        default:
            break;
        }
    }
}

ScriptComponentSerializer::ScriptComponentSerializer(const ScriptModule& module,
                                                     const ScriptComponentDef& def)
{
    ComponentName_ = std::string(module.GetString(def.Name));
    JsonKey_ = "script." + ComponentName_;
    // Same identity ScriptLink registers storage under, so the two agree.
    TypeId_ = MakeComponentTypeId(JsonKey_);
    ChunkId_ = DeriveChunkId(JsonKey_);

    const ScriptComponentLayout layout = ComputeScriptComponentLayout(def);
    Size_ = layout.Size;
    Alignment_ = layout.Alignment;

    Fields_.reserve(def.Fields.size());
    Leaves_.reserve(def.Fields.size());
    for (const ScriptComponentField& field : def.Fields)
    {
        const auto scalar = static_cast<ScriptScalarKind>(field.Scalar);
        const bool supported = IsSupportedScalar(field);

        RuntimeField rf;
        rf.Name = std::string(module.GetString(field.Name));
        rf.Offset = field.ByteOffset;
        rf.Size = ScriptScalarSize(field.Scalar);
        rf.Scalar = supported ? ToFieldScalar(scalar) : FieldScalar::Unsupported;
        rf.Count = 1;
        rf.ReadOnly = !supported;
        Fields_.push_back(std::move(rf));

        Leaves_.push_back(Leaf{ field.ByteOffset, field.Scalar, supported });
    }

    // Default image: the schema stores each field's default already in storage
    // (scalar) format, in the low bytes of DefaultRaw, so copy it straight in.
    // One default per field is broadcast to every array element.
    Default_.assign(Size_, std::byte{ 0 });
    for (const ScriptComponentField& field : def.Fields)
    {
        const std::size_t scalarSize = ScriptScalarSize(field.Scalar);
        const std::size_t copyBytes = std::min<std::size_t>(scalarSize, sizeof(field.DefaultRaw));
        const std::uint8_t count = std::max<std::uint8_t>(field.ArrayCount, 1);
        for (std::uint8_t e = 0; e < count; ++e)
            std::memcpy(Default_.data() + field.ByteOffset + e * scalarSize, &field.DefaultRaw,
                        copyBytes);
    }
}

void ScriptComponentSerializer::RegisterStorage(Registry& registry) const
{
    // Idempotent per ComponentTypeId; matches ScriptLink's raw registration so a
    // module linked in the runtime and its serializer agree on one storage id.
    registry.Components.RegisterComponentRaw(ComponentName_, TypeId_, Size_, Alignment_,
                                             /*isTag*/ false);
}

bool ScriptComponentSerializer::HasComponent(EntityId entity, const Registry& registry) const
{
    const ComponentId id = registry.Components.GetComponentIdByType(TypeId_);
    return id != InvalidComponentId && registry.Components.HasComponent(entity, id);
}

bool ScriptComponentSerializer::Save(IWriteArchive& archive, EntityId entity,
                                     const Registry& registry, SceneSerializationContext&) const
{
    const ComponentId id = registry.Components.GetComponentIdByType(TypeId_);
    if (id == InvalidComponentId)
        return true;
    const void* raw = registry.Components.GetComponentRaw(entity, id);
    if (raw == nullptr)
        return true; // entity lacks the component: nothing to write

    const auto* bytes = static_cast<const std::byte*>(raw);
    // Fields live in the component's own object (keyed by JsonKey by the caller),
    // the same shape the templated ComponentSerializer emits.
    archive.BeginObject(std::string_view{});
    for (std::size_t i = 0; i < Leaves_.size(); ++i)
    {
        const Leaf& leaf = Leaves_[i];
        if (!leaf.Supported)
            continue;
        WriteLeaf(archive, Fields_[i].Name, bytes + leaf.Offset,
                  static_cast<ScriptScalarKind>(leaf.Scalar));
    }
    archive.End();
    return archive.Ok();
}

bool ScriptComponentSerializer::Load(IReadArchive& archive, EntityId entity, Registry& registry,
                                     SceneSerializationContext&)
{
    const ComponentId id = registry.Components.GetComponentIdByType(TypeId_);
    if (id == InvalidComponentId)
        return false; // storage must be registered (RegisterStorage) before load

    // Start from the declared defaults so unsupported and absent fields keep
    // their intended values, then overwrite each supported leaf from the archive.
    std::vector<std::byte> bytes = Default_;
    archive.BeginObject(std::string_view{});
    for (std::size_t i = 0; i < Leaves_.size(); ++i)
    {
        const Leaf& leaf = Leaves_[i];
        if (!leaf.Supported)
            continue;
        ReadLeaf(archive, Fields_[i].Name, bytes.data() + leaf.Offset,
                 static_cast<ScriptScalarKind>(leaf.Scalar));
    }
    archive.End();

    registry.Components.AddComponentRaw(entity, id, bytes.data(), Size_, Alignment_,
                                        /*onAdd*/ nullptr);
    return true;
}

bool ScriptComponentSerializer::Remove(EntityId entity, Registry& registry) const
{
    const ComponentId id = registry.Components.GetComponentIdByType(TypeId_);
    if (id != InvalidComponentId && registry.Components.HasComponent(entity, id))
        registry.Components.RemoveComponentRaw(entity, id, /*onRemove*/ nullptr);
    return true;
}

void RegisterScriptComponentSerializers(const ScriptModule& module)
{
    for (const ScriptComponentDef& def : module.Components)
        RegisterComponentSerializer(std::make_unique<ScriptComponentSerializer>(module, def));
}
