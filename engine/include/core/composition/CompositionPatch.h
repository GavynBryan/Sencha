#pragma once

#include <core/composition/Composition.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string>

// The patch that, composed over `baseline` as a local patch, gives `live`:
// members that differ, `null` for members `live` lacks, and a keyed list as an
// object keyed by element. Nothing when the two are equal.
template <typename V>
[[nodiscard]] std::optional<V> PatchFrom(const V& baseline, const V& live, const DataFieldSchema* schema)
{
    using Traits = CompositionValueTraits<V>;
    const auto keyOf = [](const V& element, const DataFieldSchema& key) -> const V* {
        const V* stated = Traits::IsObject(element) ? Traits::FindMember(element, key.Key) : nullptr;
        return stated != nullptr && Traits::IsString(*stated) ? stated : nullptr;
    };
    const auto elementKeyed = [&](const V& list, const DataFieldSchema& key, std::string_view name) -> const V* {
        for (std::size_t i = 0; i < Traits::Size(list); ++i)
            if (const V* stated = keyOf(Traits::Element(list, i), key); stated != nullptr && Traits::AsString(*stated) == name)
                return &Traits::Element(list, i);
        return nullptr;
    };

    schema = UnwrapDataField(schema);
    if (schema != nullptr && schema->Kind == DataFieldKind::Array && Traits::IsArray(live) && Traits::IsArray(baseline))
        if (const DataFieldSchema* key = ArrayMergeKey(*schema))
        {
            const DataFieldSchema* element = DataFieldElement(*schema);
            V patch = Traits::MakeObject();
            bool changed = false;
            for (std::size_t i = 0; i < Traits::Size(live); ++i)
            {
                const V& item = Traits::Element(live, i);
                const V* name = keyOf(item, *key);
                if (name == nullptr)
                    return live;
                if (const V* was = elementKeyed(baseline, *key, Traits::AsString(*name)))
                {
                    if (std::optional<V> inner = PatchFrom(*was, item, element))
                    {
                        Traits::AppendMember(patch, Traits::AsString(*name), std::move(*inner));
                        changed = true;
                    }
                }
                else
                {
                    Traits::AppendMember(patch, Traits::AsString(*name), item);
                    changed = true;
                }
            }
            for (std::size_t i = 0; i < Traits::Size(baseline); ++i)
                if (const V* name = keyOf(Traits::Element(baseline, i), *key);
                    name != nullptr && elementKeyed(live, *key, Traits::AsString(*name)) == nullptr)
                {
                    Traits::AppendMember(patch, Traits::AsString(*name), Traits::MakeNull());
                    changed = true;
                }
            return changed ? std::optional<V>(std::move(patch)) : std::nullopt;
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
