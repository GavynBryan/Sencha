#pragma once

#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

enum class CompositionLayer : std::uint8_t
{
    Base,
    Include,
    Local,
};

enum class CompositionNull : std::uint8_t
{
    Removes,
    IsAValue,
};

struct CompositionOrigin
{
    CompositionLayer Layer = CompositionLayer::Local;
    std::string Asset;
};

using CompositionProvenance = std::unordered_map<std::string, CompositionOrigin>;

struct CompositionDiagnostic
{
    std::string Code;
    std::string Path;
    std::string Asset;
    std::string Message;
};

struct CompositionReport
{
    std::vector<CompositionDiagnostic> Diagnostics;
    CompositionProvenance Provenance;
    std::vector<std::string> Removed;
};

struct CompositionPolicy
{
    const DataFieldSchema* Schema = nullptr;
    CompositionNull Null = CompositionNull::Removes;
    bool Provenance = false;
};

class JsonValue;

template <typename V>
struct CompositionValueTraits;

template <typename V>
struct CompositionLayerInput
{
    const V* Value = nullptr;
    std::string_view Asset;
    const CompositionProvenance* Provenance = nullptr;
};

[[nodiscard]] const CompositionOrigin* FindCompositionOrigin(const CompositionProvenance& provenance,
                                                             std::string_view path);

[[nodiscard]] std::string CompositionLogicalPath(const JsonValue& composed, const DataFieldSchema& root,
                                                 std::string_view indexPath);

namespace CompositionDetail
{
    [[nodiscard]] std::string_view ParentPath(std::string_view path);
    [[nodiscard]] bool IsUnder(std::string_view path, std::string_view ancestor);

    template <typename V>
    class Composer
    {
        using T = CompositionValueTraits<V>;

    public:
        Composer(const CompositionPolicy& policy, CompositionReport& report)
            : Policy(policy)
            , Report(report)
        {
        }

        void Layer(CompositionLayer layer, std::string_view asset, const CompositionProvenance* provenance,
                   std::size_t include = 0)
        {
            CurrentLayer = layer;
            CurrentAsset = asset;
            SourceProvenance = provenance;
            CurrentInclude = include;
        }

        void Merge(V& target, const V& patch, const DataFieldSchema* node, const std::string& path)
        {
            node = UnwrapDataField(node);
            if (node == nullptr && (!T::IsObject(target) || !T::IsObject(patch)))
            {
                WriteLeaf(target, patch, path);
                return;
            }
            if (node != nullptr && node->Kind == DataFieldKind::Array)
            {
                const DataFieldSchema* key = ArrayMergeKey(*node);
                if (key == nullptr && T::IsObject(patch))
                {
                    Diagnose("keyed_patch_on_unkeyed_array", path,
                             "this list is not keyed, so it is written as a whole list");
                    return;
                }
                if (key != nullptr && T::IsArray(patch) && CurrentLayer == CompositionLayer::Local
                    && node->MergePolicy == DataMergePolicy::Override)
                {
                    ReplaceKeyed(target, patch, *node, *key, path);
                    return;
                }
                if (key != nullptr)
                {
                    MergeKeyed(target, patch, *node, *key, path);
                    return;
                }
                WriteLeaf(target, patch, path);
                return;
            }
            if (!T::IsObject(patch))
            {
                WriteLeaf(target, patch, path);
                return;
            }
            if (!T::IsObject(target))
            {
                ClearUnder(path);
                target = T::MakeObject();
            }
            T::ForEachMember(patch, [&](std::string_view name, const V& value) {
                const DataFieldSchema* child =
                    node != nullptr && node->Kind == DataFieldKind::Record ? FindChild(*node, name) : nullptr;
                const std::string childPath = path + "." + std::string(name);
                if (T::IsNull(value) && Policy.Null == CompositionNull::Removes)
                {
                    RemoveMember(target, name, child, childPath);
                    return;
                }
                if (T::FindMember(target, name) == nullptr)
                {
                    if (child == nullptr)
                    {
                        T::AppendMember(target, name, V{});
                        WriteLeaf(*T::FindMember(target, name), value, childPath);
                        return;
                    }
                    T::AppendMember(target, name, EmptyFor(*child, value));
                }
                Merge(*T::FindMember(target, name), value, child, childPath);
            });
        }

        void RecordAll(const V& value, const DataFieldSchema* node, const std::string& path)
        {
            node = UnwrapDataField(node);
            if (T::IsObject(value) && (node == nullptr || node->Kind == DataFieldKind::Record))
            {
                bool members = false;
                T::ForEachMember(value, [&](std::string_view name, const V& member) {
                    members = true;
                    const DataFieldSchema* child =
                        node != nullptr ? FindChild(*node, name) : nullptr;
                    RecordAll(member, child, path + "." + std::string(name));
                });
                if (!members)
                    RecordOrigin(path);
                return;
            }
            const DataFieldSchema* key =
                node != nullptr && node->Kind == DataFieldKind::Array ? ArrayMergeKey(*node) : nullptr;
            if (key != nullptr && T::IsArray(value))
            {
                for (std::size_t i = 0; i < T::Size(value); ++i)
                {
                    const V& element = T::Element(value, i);
                    const V* name = T::IsObject(element) ? T::FindMember(element, key->Key) : nullptr;
                    if (name == nullptr || !T::IsString(*name))
                        continue;
                    const std::string elementPath = path + "{" + std::string(T::AsString(*name)) + "}";
                    RecordOrigin(elementPath);
                    RecordAll(element, &node->Children.front(), elementPath);
                }
                return;
            }
            RecordOrigin(path);
        }

        void ReportConflicts(std::string_view localAsset)
        {
            for (const Conflict& conflict : Conflicts)
            {
                const bool resolved = std::ranges::any_of(LocalWrites, [&](const std::string& written) {
                    return IsUnder(conflict.Path, written);
                });
                if (resolved)
                    continue;
                Report.Diagnostics.push_back(
                    { "include_conflict", conflict.Path, std::string(localAsset),
                      std::format("'{}' and '{}' set different values; state the value here to choose",
                                  conflict.First, conflict.Second) });
            }
        }

    private:
        struct IncludeWrite
        {
            std::size_t Include = 0;
            std::string Asset;
            V Value;
        };

        struct Conflict
        {
            std::string Path;
            std::string First;
            std::string Second;
        };

        void Diagnose(std::string code, std::string_view path, std::string message)
        {
            Report.Diagnostics.push_back(
                { std::move(code), std::string(path), std::string(CurrentAsset), std::move(message) });
        }

        [[nodiscard]] V EmptyFor(const DataFieldSchema& child, const V& value) const
        {
            const DataFieldSchema* node = UnwrapDataField(&child);
            if (node->Kind == DataFieldKind::Array)
                return T::MakeArray();
            if (node->Kind == DataFieldKind::Record && T::IsObject(value))
                return T::MakeObject();
            return V{};
        }

        void WriteLeaf(V& target, const V& patch, const std::string& path)
        {
            if (CurrentLayer == CompositionLayer::Include)
                NoteIncludeWrite(path, patch);
            if (CurrentLayer == CompositionLayer::Local)
                LocalWrites.push_back(path);
            if (T::IsObject(target) || T::IsArray(target))
                ClearUnder(path);
            target = patch;
            if (Policy.Provenance)
                RecordAll(target, nullptr, path);
        }

        void NoteIncludeWrite(const std::string& path, const V& value)
        {
            for (const auto& [written, earlier] : IncludeWrites)
            {
                if (earlier.Include == CurrentInclude)
                    continue;
                if (written == path)
                {
                    if (!T::Equal(earlier.Value, value))
                        Conflicts.push_back({ path, earlier.Asset, std::string(CurrentAsset) });
                }
                else if (IsUnder(path, written))
                {
                    if (const V* within = MemberAt(earlier.Value, std::string_view(path).substr(written.size()));
                        within != nullptr && !T::Equal(*within, value))
                        Conflicts.push_back({ path, earlier.Asset, std::string(CurrentAsset) });
                }
                else if (IsUnder(written, path))
                {
                    if (const V* within = MemberAt(value, std::string_view(written).substr(path.size()));
                        within != nullptr && !T::Equal(*within, earlier.Value))
                        Conflicts.push_back({ written, earlier.Asset, std::string(CurrentAsset) });
                }
            }
            IncludeWrites.try_emplace(path, IncludeWrite{ CurrentInclude, std::string(CurrentAsset), value });
        }

        [[nodiscard]] static const V* MemberAt(const V& value, std::string_view relative)
        {
            const V* at = &value;
            while (!relative.empty() && relative.front() == '.')
            {
                relative.remove_prefix(1);
                const std::size_t end = relative.find('.');
                if (!T::IsObject(*at) || (at = T::FindMember(*at, relative.substr(0, end))) == nullptr)
                    return nullptr;
                relative.remove_prefix(end == std::string_view::npos ? relative.size() : end);
            }
            return relative.empty() ? at : nullptr;
        }

        void RemoveMember(V& target, std::string_view name, const DataFieldSchema* child, const std::string& path)
        {
            if (child != nullptr && child->Required)
            {
                Diagnose("cannot_clear_required", path, "a required field cannot be cleared");
                return;
            }
            if (CurrentLayer == CompositionLayer::Local)
                LocalWrites.push_back(path);
            if (T::EraseMember(target, name))
            {
                ClearUnder(path);
                if (CurrentLayer == CompositionLayer::Local)
                    Report.Removed.push_back(path);
            }
        }

        [[nodiscard]] std::optional<std::size_t> IndexOf(const V& array, const DataFieldSchema& key,
                                                         std::string_view name) const
        {
            for (std::size_t i = 0; i < T::Size(array); ++i)
            {
                const V& element = T::Element(array, i);
                const V* stated = T::IsObject(element) ? T::FindMember(element, key.Key) : nullptr;
                if (stated != nullptr && T::IsString(*stated) && T::AsString(*stated) == name)
                    return i;
            }
            return std::nullopt;
        }

        void ReplaceKeyed(V& target, const V& patch, const DataFieldSchema& node, const DataFieldSchema& key,
                          const std::string& path)
        {
            LocalWrites.push_back(path);
            ClearUnder(path);
            target = T::MakeArray();
            for (std::size_t i = 0; i < T::Size(patch); ++i)
            {
                const V& element = T::Element(patch, i);
                const V* stated = T::IsObject(element) ? T::FindMember(element, key.Key) : nullptr;
                if (stated == nullptr || !T::IsString(*stated))
                {
                    Diagnose("missing_key", std::format("{}[{}]", path, i),
                             std::format("each element of a keyed list states its '{}'", key.Key));
                    continue;
                }
                AddElement(target, T::AsString(*stated), element, node, key, path);
            }
        }

        void MergeKeyed(V& target, const V& patch, const DataFieldSchema& node, const DataFieldSchema& key,
                        const std::string& path)
        {
            if (!T::IsArray(target))
            {
                ClearUnder(path);
                target = T::MakeArray();
            }
            if (T::IsArray(patch))
            {
                for (std::size_t i = 0; i < T::Size(patch); ++i)
                {
                    const V& element = T::Element(patch, i);
                    const V* stated = T::IsObject(element) ? T::FindMember(element, key.Key) : nullptr;
                    if (stated == nullptr || !T::IsString(*stated))
                    {
                        Diagnose("missing_key", std::format("{}[{}]", path, i),
                                 std::format("each element of a keyed list states its '{}'", key.Key));
                        continue;
                    }
                    MergeElement(target, T::AsString(*stated), element, node, key, path);
                }
                return;
            }
            if (!T::IsObject(patch))
            {
                Diagnose("keyed_patch_not_a_list", path, "a keyed list is patched by a list or by key");
                return;
            }
            T::ForEachMember(patch, [&](std::string_view name, const V& value) {
                const std::string elementPath = path + "{" + std::string(name) + "}";
                if (key.Kind == DataFieldKind::StableKey && !IsValidStableKey(name))
                {
                    Diagnose("invalid_key", elementPath,
                             "a key is 1-64 letters, digits, underscores or hyphens");
                    return;
                }
                if (T::IsNull(value))
                {
                    RemoveElement(target, name, node, key, elementPath);
                    return;
                }
                if (!T::IsObject(value))
                {
                    Diagnose("element_not_a_record", elementPath, "an element patch is a record");
                    return;
                }
                if (const V* stated = T::FindMember(value, key.Key);
                    stated != nullptr && (!T::IsString(*stated) || T::AsString(*stated) != name))
                {
                    Diagnose("key_mismatch", elementPath + "." + key.Key,
                             std::format("'{}' must match the key it is listed under, '{}'", key.Key, name));
                    return;
                }
                MergeElement(target, name, value, node, key, path);
            });
        }

        void MergeElement(V& target, std::string_view name, const V& value, const DataFieldSchema& node,
                          const DataFieldSchema& key, const std::string& path)
        {
            const std::string elementPath = path + "{" + std::string(name) + "}";
            const std::optional<std::size_t> at = IndexOf(target, key, name);
            if (!at)
            {
                AddElement(target, name, value, node, key, path);
                return;
            }
            if (node.MergePolicy == DataMergePolicy::AppendOnly)
            {
                if (CurrentLayer == CompositionLayer::Include && T::Equal(T::Element(target, *at), value))
                    return;
                Diagnose("redeclares_inherited", elementPath,
                         std::format("'{}' is inherited; this list only takes new entries", name));
                return;
            }
            Merge(T::Element(target, *at), value, &node.Children.front(), elementPath);
        }

        void AddElement(V& target, std::string_view name, const V& value, const DataFieldSchema& node,
                        const DataFieldSchema& key, const std::string& path)
        {
            const std::string elementPath = path + "{" + std::string(name) + "}";
            V fresh = T::MakeObject();
            T::AppendMember(fresh, key.Key, T::MakeString(name));
            T::AppendElement(target, std::move(fresh));
            V& element = T::Element(target, T::Size(target) - 1);
            Merge(element, value, &node.Children.front(), elementPath);
            if (CurrentLayer == CompositionLayer::Local)
                LocalWrites.push_back(elementPath);
            RecordOrigin(elementPath);
            RecordOrigin(elementPath + "." + key.Key);
        }

        void RemoveElement(V& target, std::string_view name, const DataFieldSchema& node, const DataFieldSchema& key,
                           const std::string& elementPath)
        {
            if (node.MergePolicy == DataMergePolicy::AppendOnly)
            {
                Diagnose("removes_inherited", elementPath, "this list only takes new entries");
                return;
            }
            const std::optional<std::size_t> at = IndexOf(target, key, name);
            if (!at)
            {
                Diagnose("remove_unknown_key", elementPath,
                         std::format("nothing inherited is keyed '{}'", name));
                return;
            }
            T::EraseElement(target, *at);
            ClearUnder(elementPath);
            if (CurrentLayer == CompositionLayer::Local)
            {
                LocalWrites.push_back(elementPath);
                Report.Removed.push_back(elementPath);
            }
        }

        void RecordOrigin(const std::string& path)
        {
            if (!Policy.Provenance)
                return;
            std::string asset(CurrentAsset);
            if (SourceProvenance != nullptr)
                if (const CompositionOrigin* origin = FindCompositionOrigin(*SourceProvenance, path))
                    asset = origin->Asset;
            Report.Provenance[path] = CompositionOrigin{ CurrentLayer, std::move(asset) };
        }

        void ClearUnder(std::string_view path)
        {
            if (!Policy.Provenance)
                return;
            std::erase_if(Report.Provenance, [&](const auto& entry) { return IsUnder(entry.first, path); });
        }

        const CompositionPolicy& Policy;
        CompositionReport& Report;
        CompositionLayer CurrentLayer = CompositionLayer::Local;
        std::string_view CurrentAsset;
        const CompositionProvenance* SourceProvenance = nullptr;
        std::size_t CurrentInclude = 0;
        std::map<std::string, IncludeWrite> IncludeWrites;
        std::vector<Conflict> Conflicts;
        std::vector<std::string> LocalWrites;
    };
}

template <typename V>
void ApplyCompositionPatch(V& target, const V& patch, std::string_view asset, const CompositionPolicy& policy,
                           CompositionReport& report)
{
    CompositionDetail::Composer<V> composer(policy, report);
    composer.Layer(CompositionLayer::Local, asset, nullptr);
    composer.Merge(target, patch, policy.Schema, "$");
}

template <typename V>
[[nodiscard]] V ComposeLayers(const CompositionLayerInput<V>* base, std::span<const CompositionLayerInput<V>> includes,
                              const CompositionLayerInput<V>& local, const CompositionPolicy& policy,
                              CompositionReport& report)
{
    using T = CompositionValueTraits<V>;
    CompositionDetail::Composer<V> composer(policy, report);
    V result = base != nullptr ? *base->Value : T::MakeObject();
    if (base != nullptr && policy.Provenance)
    {
        composer.Layer(CompositionLayer::Base, base->Asset, base->Provenance);
        composer.RecordAll(result, policy.Schema, "$");
    }
    for (std::size_t i = 0; i < includes.size(); ++i)
    {
        composer.Layer(CompositionLayer::Include, includes[i].Asset, includes[i].Provenance, i);
        composer.Merge(result, *includes[i].Value, policy.Schema, "$");
    }
    composer.Layer(CompositionLayer::Local, local.Asset, nullptr);
    composer.Merge(result, *local.Value, policy.Schema, "$");
    composer.ReportConflicts(local.Asset);
    return result;
}
