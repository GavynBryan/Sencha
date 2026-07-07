#pragma once

#include <core/metadata/RuntimeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ScriptModule;
struct ScriptComponentDef;

//=============================================================================
// ScriptComponentSerializer
//
// An IComponentSerializer synthesized at runtime from a cooked T script's
// component schema (ScriptComponentDef), so a script-defined component appears
// in the editor's registry-driven inspector and round-trips through scene
// save/load exactly like a native component. Everything the templated
// ComponentSerializer<T> reads from a C++ TypeSchema (offsets, defaults, field
// names) this reads from the module schema data instead.
//
// v1 authors and persists Bool/Int32/UInt32/Float/Double leaves. Other scalar
// kinds (Entity/TagId/Int64/Color3) and fixed arrays are shown read-only in the
// inspector and keep their declared defaults across save/load (the text archive
// has no 64-bit scalar, and entity/asset leaves need the remap path a native
// handle field uses).
//=============================================================================
class ScriptComponentSerializer final : public IComponentSerializer
{
public:
    ScriptComponentSerializer(const ScriptModule& module, const ScriptComponentDef& def);

    ComponentTypeId TypeId() const override { return TypeId_; }
    std::string_view JsonKey() const override { return JsonKey_; }
    std::uint32_t BinaryChunkId() const override { return ChunkId_; }
    std::span<const RuntimeField> RuntimeFields() const override { return Fields_; }
    std::vector<std::byte> DefaultBytes() const override { return Default_; }

    void RegisterStorage(Registry& registry) const override;
    bool HasComponent(EntityId entity, const Registry& registry) const override;
    bool Save(IWriteArchive& archive, EntityId entity, const Registry& registry,
              SceneSerializationContext& context) const override;
    bool Load(IReadArchive& archive, EntityId entity, Registry& registry,
              SceneSerializationContext& context) override;
    bool Remove(EntityId entity, Registry& registry) const override;

private:
    // Per-leaf save/load descriptor, parallel to Fields_. Scalar is the raw
    // ScriptScalarKind (stored as uint8 so this header stays free of
    // ScriptModule.h); Supported gates whether the leaf round-trips.
    struct Leaf
    {
        std::size_t Offset = 0;
        std::uint8_t Scalar = 0;
        bool Supported = false;
    };

    ComponentTypeId TypeId_{};
    std::string JsonKey_;       // "script.<Name>": scene key and identity name
    std::string ComponentName_; // unprefixed, for RegisterComponentRaw meta
    std::uint32_t ChunkId_ = 0;
    std::vector<RuntimeField> Fields_;
    std::vector<Leaf> Leaves_;
    std::vector<std::byte> Default_;
    std::size_t Size_ = 0;
    std::size_t Alignment_ = 1;
};

// Builds one ScriptComponentSerializer per component declared in the module and
// registers each into the process serializer registry. Idempotent on the
// identity triple, so re-linking the same module (a second zone, a hot reload)
// is a no-op. Called at module link in the runtime and at project load in the
// editor.
void RegisterScriptComponentSerializers(const ScriptModule& module);
