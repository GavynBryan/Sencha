#pragma once

#include <core/composition/Composition.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string>
#include <vector>

namespace CompositionPatchDetail
{
    template <typename V>
    [[nodiscard]] const V* KeyOf(const V& element, const DataFieldSchema& key)
    {
        using Traits = CompositionValueTraits<V>;
        const V* stated = Traits::IsObject(element) ? Traits::FindMember(element, key.Key) : nullptr;
        if (stated == nullptr || !Traits::IsString(*stated))
            return nullptr;
        if (key.Kind == DataFieldKind::StableKey && !IsValidStableKey(Traits::AsString(*stated)))
            return nullptr;
        return stated;
    }

    template <typename V>
    [[nodiscard]] const V* ElementKeyed(const V& list, const DataFieldSchema& key, std::string_view name)
    {
        using Traits = CompositionValueTraits<V>;
        for (std::size_t i = 0; i < Traits::Size(list); ++i)
            if (const V* stated = KeyOf(Traits::Element(list, i), key); stated != nullptr && Traits::AsString(*stated) == name)
                return &Traits::Element(list, i);
        return nullptr;
    }

    template <typename V>
    [[nodiscard]] std::optional<std::vector<std::string_view>> UniqueKeys(const V& list, const DataFieldSchema& key)
    {
        using Traits = CompositionValueTraits<V>;
        std::vector<std::string_view> keys;
        for (std::size_t i = 0; i < Traits::Size(list); ++i)
        {
            const V* stated = KeyOf(Traits::Element(list, i), key);
            if (stated == nullptr || std::ranges::find(keys, Traits::AsString(*stated)) != keys.end())
                return std::nullopt;
            keys.push_back(Traits::AsString(*stated));
        }
        return keys;
    }

    [[nodiscard]] inline bool KeptInOrderThenNew(const std::vector<std::string_view>& baseline,
                                                 const std::vector<std::string_view>& live)
    {
        std::size_t next = 0;
        for (std::string_view name : baseline)
            if (std::ranges::find(live, name) != live.end() && live[next++] != name)
                return false;
        return true;
    }
}

// The local patch that composes `baseline` into `live` (docs/assets/composition.md, "Patches from a difference").
template <typename V>
[[nodiscard]] std::optional<V> PatchFrom(const V& baseline, const V& live, const DataFieldSchema* schema)
{
    using Traits = CompositionValueTraits<V>;
    using namespace CompositionPatchDetail;

    schema = UnwrapDataField(schema);
    if (schema != nullptr && schema->Kind == DataFieldKind::Array && Traits::IsArray(live) && Traits::IsArray(baseline))
        if (const DataFieldSchema* key = ArrayMergeKey(*schema))
        {
            const bool appendOnly = schema->MergePolicy == DataMergePolicy::AppendOnly;
            const std::optional<std::vector<std::string_view>> was = UniqueKeys(baseline, *key);
            const std::optional<std::vector<std::string_view>> now = UniqueKeys(live, *key);
            const bool ordered = was && now && KeptInOrderThenNew(*was, *now);
            if (!was || !now || (!appendOnly && !ordered))
                return Traits::Equal(live, baseline) ? std::nullopt : std::optional<V>(live);

            const DataFieldSchema* element = DataFieldElement(*schema);
            V patch = Traits::MakeObject();
            V added = Traits::MakeArray();
            bool inheritedChanged = false;
            for (std::size_t i = 0; i < Traits::Size(live); ++i)
            {
                const V& item = Traits::Element(live, i);
                const std::string_view name = Traits::AsString(*KeyOf(item, *key));
                if (const V* inherited = ElementKeyed(baseline, *key, name))
                {
                    if (std::optional<V> inner = PatchFrom(*inherited, item, element))
                    {
                        Traits::AppendMember(patch, name, std::move(*inner));
                        inheritedChanged = true;
                    }
                    else if (!ordered)
                    {
                        Traits::AppendMember(patch, name, item);
                        inheritedChanged = true;
                    }
                }
                else
                {
                    Traits::AppendMember(patch, name, item);
                    Traits::AppendElement(added, item);
                }
            }
            for (std::string_view name : *was)
                if (std::ranges::find(*now, name) == now->end())
                {
                    Traits::AppendMember(patch, name, Traits::MakeNull());
                    inheritedChanged = true;
                }
            if (appendOnly && !inheritedChanged)
                return Traits::Size(added) == 0 ? std::nullopt : std::optional<V>(std::move(added));
            return Traits::Size(added) == 0 && !inheritedChanged ? std::nullopt : std::optional<V>(std::move(patch));
        }

    if (Traits::IsObject(live) && Traits::IsObject(baseline))
    {
        V patch = Traits::MakeObject();
        bool changed = false;
        Traits::ForEachMember(live, [&](std::string_view key, const V& value) {
            const V* was = Traits::FindMember(baseline, key);
            if (was == nullptr)
            {
                Traits::AppendMember(patch, key, value);
                changed = true;
                return;
            }
            const DataFieldSchema* child =
                schema != nullptr && schema->Kind == DataFieldKind::Record ? FindChild(*schema, key) : nullptr;
            if (std::optional<V> inner = PatchFrom(*was, value, child))
            {
                Traits::AppendMember(patch, key, std::move(*inner));
                changed = true;
            }
        });
        Traits::ForEachMember(baseline, [&](std::string_view key, const V&) {
            if (Traits::FindMember(live, key) == nullptr)
            {
                Traits::AppendMember(patch, key, Traits::MakeNull());
                changed = true;
            }
        });
        return changed ? std::optional<V>(std::move(patch)) : std::nullopt;
    }
    return Traits::Equal(live, baseline) ? std::nullopt : std::optional<V>(live);
}
